//===- PIC16F1.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Microchip PIC16F1 (enhanced mid-range) support.
//
// Address conventions:
//   * Program memory uses byte addresses, two per 14-bit word, as in the
//     Intel HEX files Microchip tools exchange.
//   * Data addresses are 0x800000 plus the 16-bit address the FSR registers
//     use: banked registers at 0x0000-0x0FFF, linear RAM at 0x2000 up, and
//     flash read as data at 0x8000 up.
//
// Direct addressing needs a bank number and a 7-bit offset. For a symbol in
// linear RAM the linker converts: the linear region holds the 80 general
// purpose bytes of each bank back to back, so linear offset n is bank n / 80,
// file address 0x20 + n % 80.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Target.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class PIC16F1 final : public TargetInfo {
public:
  PIC16F1(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;

private:
  /// The banked form (bank << 7 | offset) of a data address, or -1.
  int64_t bankedAddress(uint64_t val) const;
};
} // namespace

PIC16F1::PIC16F1(Ctx &ctx) : TargetInfo(ctx) {
  // Unprogrammed flash reads 0x3FFF; filling gaps with it keeps the image
  // identical to what a programmer would leave there.
  trapInstr = {0xFF, 0x3F, 0xFF, 0x3F};
  defaultImageBase = 0;
  defaultMaxPageSize = 2;
  defaultCommonPageSize = 2;
}

RelExpr PIC16F1::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  return type == R_PIC16F1_BRA9 ? R_PC : R_ABS;
}

static const uint64_t dataSpace = 0x800000;
static const uint64_t linearBase = 0x2000;
static const uint64_t linearEnd = 0x2FF0; // 51 banks of 80 bytes at most
static const uint64_t bankedEnd = 0x2000; // 64 banks at most

int64_t PIC16F1::bankedAddress(uint64_t val) const {
  if (val < dataSpace)
    return -1;
  uint64_t a = val - dataSpace;
  if (a < bankedEnd)
    return a;
  if (a >= linearBase && a < linearEnd) {
    uint64_t n = a - linearBase;
    return ((n / 80) << 7) | (0x20 + n % 80);
  }
  return -1;
}

void PIC16F1::relocate(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  // Replace the low `bits` bits of the instruction word.
  auto patch = [&](unsigned bits, uint64_t v) {
    uint16_t mask = (1u << bits) - 1;
    write16le(loc, (read16le(loc) & ~mask) | (v & mask));
  };

  switch (rel.type) {
  case R_PIC16F1_8:
    *loc = val;
    break;
  case R_PIC16F1_16:
    write16le(loc, val);
    break;
  case R_PIC16F1_32:
    write32le(loc, val);
    break;
  case R_PIC16F1_PM16:
    checkAlignment(ctx, loc, val, 2, rel);
    write16le(loc, val >> 1);
    break;
  case R_PIC16F1_LO8:
    patch(8, val);
    break;
  case R_PIC16F1_HI8:
    patch(8, val >> 8);
    break;
  case R_PIC16F1_PM_LO8:
    checkAlignment(ctx, loc, val, 2, rel);
    patch(8, val >> 1);
    break;
  case R_PIC16F1_PM_HI8:
    patch(8, val >> 9);
    break;
  case R_PIC16F1_CALL11:
    checkAlignment(ctx, loc, val, 2, rel);
    patch(11, val >> 1);
    break;
  case R_PIC16F1_PAGE7:
    patch(7, val >> 9);
    break;
  case R_PIC16F1_BRA9: {
    // val is the target minus the address of the BRA; the offset counts
    // words from the instruction after it.
    int64_t offset = ((int64_t)val - 2) >> 1;
    checkAlignment(ctx, loc, val, 2, rel);
    checkInt(ctx, loc, offset, 9, rel);
    patch(9, offset);
    break;
  }
  case R_PIC16F1_BANK5:
  case R_PIC16F1_BANK6:
  case R_PIC16F1_F7: {
    int64_t banked = bankedAddress(val);
    if (banked < 0) {
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
               << " against '" << rel.sym
               << "': address is not reachable with banked addressing";
      break;
    }
    if (rel.type == R_PIC16F1_F7) {
      patch(7, banked);
    } else if (rel.type == R_PIC16F1_BANK6) {
      patch(6, banked >> 7);
    } else if ((banked >> 7) > 31) {
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
               << " against '" << rel.sym
               << "': address is beyond bank 31, which this device's MOVLB "
                  "cannot select";
    } else {
      patch(5, banked >> 7);
    }
    break;
  }
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void elf::setPIC16F1TargetInfo(Ctx &ctx) { ctx.target.reset(new PIC16F1(ctx)); }

//===----------------------------------------------------------------------===//
// Stack frames or static frames.
//
// The compiler emits a function that might or might not be re-entered in
// several versions (llvm/lib/Target/PIC16F1/PIC16F1StaticFrames.cpp):
//   F      with its frame on the stack; every ordinary call goes to it
//   F.sf   with a static frame
//   F.isr  a second static version with variables of its own, whose calls
//          go to "G$isr" in place of G
// Each object file also says, in a section .pic16f1.frames, which functions
// it defines and what each calls (PIC16F1AsmPrinter.cpp has the format).
//
// With every call of the program in view, this finds the functions that can
// never be entered while they are running and points F at a static version
// for those. The versions not chosen are then unreferenced and
// --gc-sections drops them. A function keeps its stack frame if
//   - it is on a cycle of calls;
//   - code that says nothing about its calls can reach it: an object file
//     without the section (hand-written assembly, say) is taken to call
//     everything it refers to, from anywhere.
// A call through a pointer is taken to reach every function whose address
// is taken.
//
// A function that both the main program and an interrupt handler can reach
// has two ways out. It can keep its stack frame, or be in the program
// twice: F.sf for the main program and F.isr for the handlers, which call
// F$isr. Two copies cost flash and save time (a static frame needs no
// setting up, and leaves FSR1 free as a second pointer). By default the
// copy is made only if the two static versions together are no larger than
// the stack version; with -z pic16f1-interrupt-copies it is made whenever
// it can be. It can be made when everything that calls the function from a
// handler does so by the $isr name: a handler itself, or the interrupt
// copy of another function.
//===----------------------------------------------------------------------===//

namespace {
struct FrameNode {
  Defined *staticVersion = nullptr;
  Defined *interruptVersion = nullptr;
  bool isInterrupt = false;
  bool callsThroughPointer = false;
  // Calls: the callee, whether the call is by the $isr name, and whether
  // it is made by the interrupt version of this function.
  struct Call {
    Symbol *callee;
    bool byInterruptName;
    bool inInterruptVersion;
  };
  SmallVector<Call, 4> calls;
};
} // namespace

void elf::pic16f1SelectFrames(Ctx &ctx, bool copiesWanted) {
  MapVector<Symbol *, FrameNode> nodes;
  SmallPtrSet<Symbol *, 16> addressTaken, fromUnknown;
  // "G$isr" names and the functions they stand for.
  MapVector<Symbol *, Symbol *> interruptNames;
  bool unknownCode = false;

  for (ELFFileBase *file : ctx.objectFiles) {
    InputSectionBase *records = nullptr;
    for (InputSectionBase *sec : file->getSections())
      if (sec && sec != &InputSection::discarded &&
          sec->name == ".pic16f1.frames")
        records = sec;
    if (!records) {
      // Whatever this file refers to and does not define, it may call.
      unknownCode = true;
      for (Symbol *sym : file->getGlobalSymbols())
        if (sym->file != file)
          fromUnknown.insert(sym);
      continue;
    }
    StringMap<Symbol *> locals;
    for (Symbol *sym : file->getLocalSymbols())
      if (sym->isDefined() && !sym->getName().empty())
        locals[sym->getName()] = sym;
    auto lookup = [&](StringRef name) -> Symbol * {
      auto it = locals.find(name);
      return it != locals.end() ? it->second : ctx.symtab->find(name);
    };
    SmallVector<SmallVector<StringRef, 4>, 32> lines;
    StringRef text = toStringRef(records->content());
    while (!text.empty()) {
      StringRef line;
      std::tie(line, text) = text.split('\n');
      SmallVector<StringRef, 4> words;
      line.split(words, ' ', -1, /*KeepEmpty=*/false);
      if (words.size() >= 2)
        lines.push_back(std::move(words));
    }
    // The $isr names first: calls are recorded by them.
    DenseMap<Symbol *, Symbol *> standsFor;
    for (auto &words : lines)
      if (words[0] == "I" && words.size() > 2)
        if (Symbol *alias = lookup(words[1]))
          if (Symbol *of = lookup(words[2])) {
            standsFor[alias] = of;
            interruptNames[alias] = of;
          }
    FrameNode *current = nullptr;
    bool inInterruptVersion = false;
    for (auto &words : lines) {
      Symbol *sym = lookup(words[1]);
      if (!sym)
        continue;
      if (words[0] == "F") {
        current = &nodes[sym];
        inInterruptVersion = false;
        if (words.size() > 2) {
          current->isInterrupt |= words[2].contains('i');
          current->callsThroughPointer |= words[2].contains('p');
        }
      } else if (words[0] == "V" && words.size() > 2) {
        current = &nodes[sym];
        auto *version = dyn_cast_or_null<Defined>(lookup(words[2]));
        inInterruptVersion = words.size() > 3 && words[3] == "i";
        if (inInterruptVersion)
          current->interruptVersion = version;
        else
          current->staticVersion = version;
      } else if (words[0] == "C" && current) {
        auto it = standsFor.find(sym);
        if (it != standsFor.end())
          current->calls.push_back({it->second, true, inInterruptVersion});
        else
          current->calls.push_back({sym, false, inInterruptVersion});
      } else if (words[0] == "A") {
        addressTaken.insert(sym);
      }
    }
    records->markDead();
  }
  if (nodes.empty())
    return;
  // Code that tells nothing may also hold pointers to functions.
  if (unknownCode)
    for (Symbol *sym : addressTaken)
      fromUnknown.insert(sym);

  // Everything reachable from the given functions, themselves included.
  auto reachable = [&](ArrayRef<Symbol *> from) {
    SmallPtrSet<Symbol *, 32> seen;
    SmallVector<Symbol *, 32> work;
    auto push = [&](Symbol *sym) {
      if (seen.insert(sym).second)
        work.push_back(sym);
    };
    for (Symbol *sym : from)
      push(sym);
    while (!work.empty()) {
      auto it = nodes.find(work.pop_back_val());
      if (it == nodes.end())
        continue;
      for (const FrameNode::Call &call : it->second.calls)
        push(call.callee);
      if (it->second.callsThroughPointer)
        for (Symbol *callee : addressTaken)
          push(callee);
    }
    return seen;
  };

  SmallVector<Symbol *, 4> mainRoots, interruptRoots;
  if (Symbol *entry = ctx.symtab->find(ctx.arg.entry))
    mainRoots.push_back(entry);
  for (auto &[sym, node] : nodes)
    if (node.isInterrupt)
      interruptRoots.push_back(sym);
  SmallVector<Symbol *, 16> unknownRoots(fromUnknown.begin(), fromUnknown.end());
  auto inMain = reachable(mainRoots);
  auto inInterrupt = reachable(interruptRoots);
  auto inUnknown = reachable(unknownRoots);

  // Why a function cannot have a static frame at all, or null.
  auto whyStack = [&](Symbol *sym, FrameNode &node) -> const char * {
    if (inUnknown.count(sym))
      return "reachable from code that does not describe its calls";
    SmallVector<Symbol *, 8> callees;
    for (const FrameNode::Call &call : node.calls)
      callees.push_back(call.callee);
    if (node.callsThroughPointer)
      callees.append(addressTaken.begin(), addressTaken.end());
    if (reachable(callees).count(sym))
      return "may call itself";
    return nullptr;
  };

  // The functions to have in the program twice. Start with all that could
  // be, then drop those that something calls from a handler by their plain
  // name: a function that keeps its stack frame or has no interrupt copy,
  // or a call through a pointer.
  SmallPtrSet<Symbol *, 16> twice;
  for (auto &[sym, node] : nodes) {
    if (!inMain.count(sym) || !inInterrupt.count(sym) || !node.staticVersion ||
        !node.interruptVersion || !isa<Defined>(sym) ||
        addressTaken.count(sym) || whyStack(sym, node))
      continue;
    uint64_t one = cast<Defined>(sym)->size;
    uint64_t two = node.staticVersion->size + node.interruptVersion->size;
    if (copiesWanted || two <= one)
      twice.insert(sym);
  }
  // Which functions run in a handler by way of their interrupt copy.
  auto usesInterruptVersion = [&](Symbol *sym, const FrameNode &node) {
    return node.isInterrupt || twice.count(sym) ||
           (!inMain.count(sym) && node.interruptVersion &&
            !inUnknown.count(sym));
  };
  // What actually runs in a handler, given the copies: from the handlers,
  // through the calls each function makes there. For a function that runs
  // there as its interrupt version those are that version's calls only.
  auto runsInHandler = [&] {
    SmallPtrSet<Symbol *, 32> seen;
    SmallVector<Symbol *, 32> work;
    auto push = [&](Symbol *sym) {
      if (seen.insert(sym).second)
        work.push_back(sym);
    };
    for (Symbol *sym : interruptRoots)
      push(sym);
    while (!work.empty()) {
      Symbol *sym = work.pop_back_val();
      auto it = nodes.find(sym);
      if (it == nodes.end())
        continue;
      const FrameNode &node = it->second;
      bool viaCopy = usesInterruptVersion(sym, node) && !node.isInterrupt;
      for (const FrameNode::Call &call : node.calls)
        if (!viaCopy || call.inInterruptVersion)
          push(call.callee);
      if (node.callsThroughPointer)
        for (Symbol *callee : addressTaken)
          push(callee);
    }
    return seen;
  };
  for (bool changed = true; changed;) {
    changed = false;
    for (Symbol *sym : runsInHandler()) {
      auto it = nodes.find(sym);
      if (it == nodes.end())
        continue;
      const FrameNode &node = it->second;
      bool viaCopy = usesInterruptVersion(sym, node);
      for (const FrameNode::Call &call : node.calls) {
        if (viaCopy && !node.isInterrupt && !call.inInterruptVersion)
          continue;
        // A call by the plain name from code that runs in a handler: the
        // callee cannot have a copy for handlers that nothing would call.
        if (!call.byInterruptName && twice.erase(call.callee))
          changed = true;
      }
      if (node.callsThroughPointer)
        for (Symbol *callee : addressTaken)
          if (twice.erase(callee))
            changed = true;
    }
  }

  auto pointAt = [](Symbol *sym, Defined *version) {
    auto *d = cast<Defined>(sym);
    d->section = version->section;
    d->value = version->value;
    d->size = version->size;
  };
  for (auto &[sym, node] : nodes) {
    if (!node.staticVersion || !isa<Defined>(sym))
      continue;
    if (const char *why = whyStack(sym, node)) {
      Log(ctx) << "stack frame for " << sym->getName() << ": " << why;
      continue;
    }
    bool main = inMain.count(sym), interrupt = inInterrupt.count(sym);
    if (main && interrupt && !twice.count(sym)) {
      Log(ctx) << "stack frame for " << sym->getName()
               << ": reachable from the main program and from an interrupt "
                  "handler";
      continue;
    }
    if (interrupt && !main && node.interruptVersion) {
      Log(ctx) << "static frame for " << sym->getName()
               << " (interrupt version)";
      pointAt(sym, node.interruptVersion);
      continue;
    }
    Log(ctx) << "static frame for " << sym->getName()
             << (twice.count(sym) ? ", and a copy for interrupt handlers" : "");
    pointAt(sym, node.staticVersion);
  }

  // G$isr is the interrupt copy of G where there is one, and G otherwise.
  for (auto &[alias, of] : interruptNames) {
    auto *target = dyn_cast<Defined>(of);
    auto it = nodes.find(of);
    if (twice.count(of) && it != nodes.end())
      target = it->second.interruptVersion;
    if (!target)
      continue;
    Defined(ctx, target->file, alias->getName(), STB_GLOBAL, alias->stOther,
            target->type, target->value, target->size, target->section)
        .overwrite(*alias);
  }
}
