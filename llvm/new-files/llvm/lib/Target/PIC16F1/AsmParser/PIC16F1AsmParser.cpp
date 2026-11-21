//===-- PIC16F1AsmParser.cpp - Parse PIC16F1 assembly ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Syntax follows Microchip's: "addwf f, W|F", "moviw ++FSR0", "moviw 3[FSR1]",
// "addfsr FSR0, -4". Mnemonics and the keywords W, F, FSR0 and FSR1 are not
// case sensitive. Address parts are written lo8(x), hi8(x), pm_lo8(x),
// pm_hi8(x), pm(x), bank(x), off(x) and page(x). A symbol whose name is one
// of the keywords must be written in parentheses.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PIC16F1MCAsmInfo.h"
#include "MCTargetDesc/PIC16F1MCTargetDesc.h"
#include "TargetInfo/PIC16F1TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"

#define DEBUG_TYPE "pic16f1-asm-parser"

using namespace llvm;

namespace {

class PIC16F1AsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;
  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  ParseStatus parseDirective(AsmToken DirectiveID) override {
    return ParseStatus::NoMatch;
  }
  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) override;

  bool parseOperand(OperandVector &Operands);
  /// Parses FSR0 or FSR1 if it is the current token.
  bool parseFSR(unsigned &N);

  MCAsmParser &getParser() const { return Parser; }
  AsmLexer &getLexer() const { return Parser.getLexer(); }

#define GET_ASSEMBLER_HEADER
#include "PIC16F1GenAsmMatcher.inc"

public:
  PIC16F1AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                   const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

class PIC16F1Operand : public MCParsedAsmOperand {
  enum KindTy { k_Tok, k_Imm, k_Dest, k_FSR, k_IndMode } Kind;
  union {
    StringRef Tok;
    const MCExpr *Imm;
    unsigned Val;
  };
  SMLoc Start, End;

public:
  PIC16F1Operand(StringRef Tok, SMLoc S)
      : Kind(k_Tok), Tok(Tok), Start(S), End(S) {}
  PIC16F1Operand(const MCExpr *Imm, SMLoc S, SMLoc E)
      : Kind(k_Imm), Imm(Imm), Start(S), End(E) {}
  PIC16F1Operand(KindTy K, unsigned V, SMLoc S, SMLoc E)
      : Kind(K), Val(V), Start(S), End(E) {}

  bool isToken() const override { return Kind == k_Tok; }
  bool isImm() const override { return Kind == k_Imm; }
  bool isReg() const override { return false; }
  bool isMem() const override { return false; }
  MCRegister getReg() const override { return MCRegister(); }
  bool isFSR() const { return Kind == k_FSR; }
  bool isIndMode() const { return Kind == k_IndMode; }
  /// W or F, or the constants 0 and 1.
  bool isDest() const {
    if (Kind == k_Dest)
      return true;
    if (Kind != k_Imm)
      return false;
    const auto *CE = dyn_cast<MCConstantExpr>(Imm);
    return CE && (CE->getValue() == 0 || CE->getValue() == 1);
  }

  StringRef getToken() const { return Tok; }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    llvm_unreachable("no register operands");
  }
  void addImmOperands(MCInst &Inst, unsigned N) const {
    if (const auto *CE = dyn_cast<MCConstantExpr>(Imm))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Imm));
  }
  void addDestOperands(MCInst &Inst, unsigned N) const {
    if (Kind == k_Dest)
      Inst.addOperand(MCOperand::createImm(Val));
    else
      Inst.addOperand(MCOperand::createImm(cast<MCConstantExpr>(Imm)->getValue()));
  }
  void addFSROperands(MCInst &Inst, unsigned N) const {
    Inst.addOperand(MCOperand::createImm(Val));
  }
  void addIndModeOperands(MCInst &Inst, unsigned N) const {
    Inst.addOperand(MCOperand::createImm(Val));
  }

  static std::unique_ptr<PIC16F1Operand> createToken(StringRef S, SMLoc L) {
    return std::make_unique<PIC16F1Operand>(S, L);
  }
  static std::unique_ptr<PIC16F1Operand> createImm(const MCExpr *E, SMLoc S,
                                                   SMLoc End) {
    return std::make_unique<PIC16F1Operand>(E, S, End);
  }
  static std::unique_ptr<PIC16F1Operand> createDest(unsigned V, SMLoc S) {
    return std::make_unique<PIC16F1Operand>(k_Dest, V, S, S);
  }
  static std::unique_ptr<PIC16F1Operand> createFSR(unsigned V, SMLoc S) {
    return std::make_unique<PIC16F1Operand>(k_FSR, V, S, S);
  }
  static std::unique_ptr<PIC16F1Operand> createIndMode(unsigned V, SMLoc S) {
    return std::make_unique<PIC16F1Operand>(k_IndMode, V, S, S);
  }

  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  void print(raw_ostream &O, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Tok:
      O << "Token " << Tok;
      break;
    case k_Imm:
      O << "Immediate ";
      MAI.printExpr(O, *Imm);
      break;
    case k_Dest:
      O << "Dest " << Val;
      break;
    case k_FSR:
      O << "FSR" << Val;
      break;
    case k_IndMode:
      O << "IndMode " << Val;
      break;
    }
  }
};

} // end anonymous namespace

bool PIC16F1AsmParser::matchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                                               OperandVector &Operands,
                                               MCStreamer &Out,
                                               uint64_t &ErrorInfo,
                                               bool MatchingInlineAsm) {
  MCInst Inst;
  switch (MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm)) {
  case Match_Success:
    Inst.setLoc(Loc);
    Out.emitInstruction(Inst, *STI);
    return false;
  case Match_MnemonicFail:
    return Error(Loc, "unknown instruction mnemonic");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = Loc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(Loc, "too few operands for instruction");
      ErrorLoc = Operands[ErrorInfo]->getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = Loc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  default:
    return Error(Loc, "invalid instruction");
  }
}

bool PIC16F1AsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                     SMLoc &EndLoc) {
  return true; // No register names: registers are file addresses.
}

ParseStatus PIC16F1AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                               SMLoc &EndLoc) {
  return ParseStatus::NoMatch;
}

bool PIC16F1AsmParser::parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
  // spec(expr)
  if (getLexer().is(AsmToken::Identifier) &&
      getLexer().peekTok().is(AsmToken::LParen)) {
    uint16_t Spec =
        PIC16F1::parseSpecifierName(getLexer().getTok().getIdentifier());
    if (Spec != PIC16F1::S_None) {
      SMLoc S = getLexer().getLoc();
      Lex(); // specifier
      Lex(); // (
      const MCExpr *Sub;
      if (getParser().parseExpression(Sub))
        return true;
      EndLoc = getLexer().getLoc();
      if (parseToken(AsmToken::RParen, "expected ')'"))
        return true;
      Res = MCSpecifierExpr::create(Sub, Spec, getContext(), S);
      return false;
    }
  }
  return getParser().parsePrimaryExpr(Res, EndLoc, nullptr);
}

bool PIC16F1AsmParser::parseFSR(unsigned &N) {
  if (getLexer().isNot(AsmToken::Identifier))
    return false;
  StringRef Id = getLexer().getTok().getIdentifier();
  if (Id.equals_insensitive("fsr0"))
    N = 0;
  else if (Id.equals_insensitive("fsr1"))
    N = 1;
  else
    return false;
  Lex();
  return true;
}

bool PIC16F1AsmParser::parseOperand(OperandVector &Operands) {
  SMLoc S = getLexer().getLoc();
  unsigned N;

  // ++FSRn and --FSRn
  if ((getLexer().is(AsmToken::Plus) && getLexer().peekTok().is(AsmToken::Plus)) ||
      (getLexer().is(AsmToken::Minus) &&
       getLexer().peekTok().is(AsmToken::Minus))) {
    bool Inc = getLexer().is(AsmToken::Plus);
    Lex();
    Lex();
    if (!parseFSR(N))
      return Error(getLexer().getLoc(), "expected FSR0 or FSR1");
    Operands.push_back(
        PIC16F1Operand::createIndMode((N << 2) | (Inc ? 0 : 1), S));
    return false;
  }

  // FSRn, FSRn++ and FSRn--
  if (parseFSR(N)) {
    if (getLexer().is(AsmToken::Plus) || getLexer().is(AsmToken::Minus)) {
      AsmToken::TokenKind K = getLexer().getKind();
      Lex();
      if (getLexer().isNot(K))
        return Error(getLexer().getLoc(), "expected FSRn++ or FSRn--");
      Lex();
      Operands.push_back(PIC16F1Operand::createIndMode(
          (N << 2) | (K == AsmToken::Plus ? 2 : 3), S));
      return false;
    }
    Operands.push_back(PIC16F1Operand::createFSR(N, S));
    return false;
  }

  // W and F
  if (getLexer().is(AsmToken::Identifier)) {
    StringRef Id = getLexer().getTok().getIdentifier();
    bool IsW = Id.equals_insensitive("w");
    if (IsW || Id.equals_insensitive("f")) {
      Lex();
      Operands.push_back(PIC16F1Operand::createDest(IsW ? 0 : 1, S));
      return false;
    }
  }

  const MCExpr *Expr;
  if (getParser().parseExpression(Expr))
    return true;
  Operands.push_back(PIC16F1Operand::createImm(Expr, S, getLexer().getLoc()));

  // k[FSRn]
  if (getLexer().is(AsmToken::LBrac)) {
    Operands.push_back(PIC16F1Operand::createToken("[", getLexer().getLoc()));
    Lex();
    SMLoc L = getLexer().getLoc();
    if (!parseFSR(N))
      return Error(L, "expected FSR0 or FSR1");
    Operands.push_back(PIC16F1Operand::createFSR(N, L));
    if (getLexer().isNot(AsmToken::RBrac))
      return Error(getLexer().getLoc(), "expected ']'");
    Operands.push_back(PIC16F1Operand::createToken("]", getLexer().getLoc()));
    Lex();
  }
  return false;
}

bool PIC16F1AsmParser::parseInstruction(ParseInstructionInfo &Info,
                                        StringRef Name, SMLoc NameLoc,
                                        OperandVector &Operands) {
  // Mnemonics are matched in lower case.
  std::string Lower = Name.lower();
  StringRef Mnemonic = getContext().allocateString(Lower);
  Operands.push_back(PIC16F1Operand::createToken(Mnemonic, NameLoc));

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    do {
      if (parseOperand(Operands))
        return true;
    } while (parseOptionalToken(AsmToken::Comma));
  }
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }
  Lex(); // Consume the EndOfStatement.
  return false;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePIC16F1AsmParser() {
  RegisterMCAsmParser<PIC16F1AsmParser> X(getThePIC16F1Target());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "PIC16F1GenAsmMatcher.inc"
