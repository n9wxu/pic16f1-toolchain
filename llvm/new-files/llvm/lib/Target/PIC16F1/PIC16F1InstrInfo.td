//===-- PIC16F1InstrInfo.td - Instruction definitions -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Two layers:
//
//  * The 49 machine instructions, with encodings. The assembler, the
//    instruction printer and the object writer use these.
//
//  * Code-generator pseudo instructions that work on the R0-R13 byte
//    registers and their 16-bit pairs. Instruction selection and register
//    allocation see only these. PIC16F1ExpandPseudo turns each one into
//    machine instructions after register allocation.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operands
//===----------------------------------------------------------------------===//

// Destination select: W (0) or F (1).
def DestAsmOperand : AsmOperandClass { let Name = "Dest"; }
def destop : Operand<i8> {
  let PrintMethod = "printDest";
  let ParserMatchClass = DestAsmOperand;
}

// FSR0 (0) or FSR1 (1).
def FSRAsmOperand : AsmOperandClass { let Name = "FSR"; }
def fsrop : Operand<i8> {
  let PrintMethod = "printFSR";
  let ParserMatchClass = FSRAsmOperand;
}

// ++FSRn, --FSRn, FSRn++, FSRn--: bit 2 is n, bits 1-0 the mode.
def IndModeAsmOperand : AsmOperandClass { let Name = "IndMode"; }
def indmode : Operand<i8> {
  let PrintMethod = "printIndMode";
  let ParserMatchClass = IndModeAsmOperand;
}

// 7-bit file register address within the selected bank.
def fileop : Operand<i16> {
  let EncoderMethod = "encodeFile";
  let PrintMethod = "printFile";
}
def lit8 : Operand<i8> { let EncoderMethod = "encodeLit8"; }
def bankop : Operand<i8> { let EncoderMethod = "encodeBank"; }
def pageop : Operand<i8> { let EncoderMethod = "encodePage"; }
def calltarget : Operand<i16> { let EncoderMethod = "encodeCall"; }
def bratarget : Operand<OtherVT> {
  let EncoderMethod = "encodeBra";
  let OperandType = "OPERAND_PCREL";
}
def bit3 : Operand<i8>;
def off6 : Operand<i8> { let EncoderMethod = "encodeOff6"; }

// Code-generator operands.
def cc : Operand<i8>;
def brtarget : Operand<OtherVT>;

// Directly addressed data: a constant address or a global plus offset.
def memabs : Operand<i16>;
def addrabs : ComplexPattern<i16, 1, "SelectAbs", [], []>;

// Pointer in a register pair plus a small constant offset.
def memptr : Operand<i16> {
  let MIOperandInfo = (ops GPR16:$base, i8imm:$off);
}
def addrptr8 : ComplexPattern<i16, 2, "SelectPtr8", [], []>;
def addrptr16 : ComplexPattern<i16, 2, "SelectPtr16", [], []>;

// Stack slot plus constant offset. After frame-index elimination the first
// operand is an offset from the stack pointer.
def memfi : Operand<i16> {
  let MIOperandInfo = (ops i16imm:$fi, i16imm:$off);
}
def addrfi : ComplexPattern<i16, 2, "SelectFI", [frameindex], []>;

//===----------------------------------------------------------------------===//
// Selection DAG nodes
//===----------------------------------------------------------------------===//

def SDT_PICCall : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_PICCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_PICCallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_PICWrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;
def SDT_PICBrCC : SDTypeProfile<0, 4, [SDTCisVT<0, i8>, SDTCisInt<1>,
                                       SDTCisSameAs<1, 2>, SDTCisVT<3, OtherVT>]>;
def SDT_PICSelectCC : SDTypeProfile<1, 5, [SDTCisVT<1, i8>, SDTCisInt<2>,
                                           SDTCisSameAs<2, 3>, SDTCisSameAs<0, 4>,
                                           SDTCisSameAs<4, 5>]>;
def SDT_PICShiftV : SDTypeProfile<1, 2, [SDTCisSameAs<0, 1>, SDTCisInt<0>,
                                         SDTCisVT<2, i8>]>;
def SDT_PICMake16 : SDTypeProfile<1, 2, [SDTCisVT<0, i16>, SDTCisVT<1, i8>,
                                         SDTCisVT<2, i8>]>;

def PICretglue : SDNode<"PIC16F1ISD::RET_GLUE", SDTNone,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def PICretiglue : SDNode<"PIC16F1ISD::RETI_GLUE", SDTNone,
                         [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def PICcall : SDNode<"PIC16F1ISD::CALL", SDT_PICCall,
                     [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def PICcallseq_start : SDNode<"ISD::CALLSEQ_START", SDT_PICCallSeqStart,
                              [SDNPHasChain, SDNPOutGlue]>;
def PICcallseq_end : SDNode<"ISD::CALLSEQ_END", SDT_PICCallSeqEnd,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def PICwrapper : SDNode<"PIC16F1ISD::WRAPPER", SDT_PICWrapper>;
// Compare and branch: condition, left, right, target.
def PICbrcc : SDNode<"PIC16F1ISD::BR_CC", SDT_PICBrCC, [SDNPHasChain]>;
// Compare and select: condition, left, right, value if true, value if false.
def PICselectcc : SDNode<"PIC16F1ISD::SELECT_CC", SDT_PICSelectCC>;
// Shift by one bit.
def PICshl1 : SDNode<"PIC16F1ISD::SHL1", SDTIntUnaryOp>;
def PIClsr1 : SDNode<"PIC16F1ISD::LSR1", SDTIntUnaryOp>;
def PICasr1 : SDNode<"PIC16F1ISD::ASR1", SDTIntUnaryOp>;
// Shift by a run-time amount.
def PICshlv : SDNode<"PIC16F1ISD::SHLV", SDT_PICShiftV>;
def PIClsrv : SDNode<"PIC16F1ISD::LSRV", SDT_PICShiftV>;
def PICasrv : SDNode<"PIC16F1ISD::ASRV", SDT_PICShiftV>;
// Store a call argument at a constant offset from the stack pointer.
def SDT_PICStoreSP : SDTypeProfile<0, 2, [SDTCisInt<0>, SDTCisVT<1, i16>]>;
def PICstoresp : SDNode<"PIC16F1ISD::STORE_SP", SDT_PICStoreSP,
                        [SDNPHasChain, SDNPMayStore]>;
// Build a 16-bit value from a low and a high byte.
def PICmake16 : SDNode<"PIC16F1ISD::MAKE16", SDT_PICMake16>;

//===----------------------------------------------------------------------===//
// Machine instructions
//===----------------------------------------------------------------------===//

class PICInst<dag outs, dag ins, string asm, list<dag> pattern = []>
    : Instruction {
  field bits<16> Inst = 0;
  field bits<16> SoftFail = 0;
  let Namespace = "PIC16F1";
  let Size = 2;
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asm;
  let Pattern = pattern;
}

// Byte-oriented operation on a file register, result to W or the file.
class ByteOp<bits<6> op, string mnem>
    : PICInst<(outs), (ins fileop:$f, destop:$d), mnem # "\t$f, $d"> {
  bits<7> f;
  bit d;
  let Inst{13-8} = op;
  let Inst{7} = d;
  let Inst{6-0} = f;
}

class BitOp<bits<4> op, string mnem>
    : PICInst<(outs), (ins fileop:$f, bit3:$b), mnem # "\t$f, $b"> {
  bits<7> f;
  bits<3> b;
  let Inst{13-10} = op;
  let Inst{9-7} = b;
  let Inst{6-0} = f;
}

class LitOp<bits<6> op, string mnem>
    : PICInst<(outs), (ins lit8:$k), mnem # "\t$k"> {
  bits<8> k;
  let Inst{13-8} = op;
  let Inst{7-0} = k;
}

class Implied<bits<14> enc, string mnem> : PICInst<(outs), (ins), mnem> {
  let Inst{13-0} = enc;
}

def ADDWF  : ByteOp<0b000111, "addwf">;
def ADDWFC : ByteOp<0b111101, "addwfc">;
def ANDWF  : ByteOp<0b000101, "andwf">;
def ASRF   : ByteOp<0b110111, "asrf">;
def LSLF   : ByteOp<0b110101, "lslf">;
def LSRF   : ByteOp<0b110110, "lsrf">;
def COMF   : ByteOp<0b001001, "comf">;
def DECF   : ByteOp<0b000011, "decf">;
def INCF   : ByteOp<0b001010, "incf">;
def IORWF  : ByteOp<0b000100, "iorwf">;
def MOVF   : ByteOp<0b001000, "movf">;
def RLF    : ByteOp<0b001101, "rlf">;
def RRF    : ByteOp<0b001100, "rrf">;
def SUBWF  : ByteOp<0b000010, "subwf">;
def SUBWFB : ByteOp<0b111011, "subwfb">;
def SWAPF  : ByteOp<0b001110, "swapf">;
def XORWF  : ByteOp<0b000110, "xorwf">;
def DECFSZ : ByteOp<0b001011, "decfsz">;
def INCFSZ : ByteOp<0b001111, "incfsz">;

def CLRF : PICInst<(outs), (ins fileop:$f), "clrf\t$f"> {
  bits<7> f;
  let Inst{13-7} = 0b0000011;
  let Inst{6-0} = f;
}
def MOVWF : PICInst<(outs), (ins fileop:$f), "movwf\t$f"> {
  bits<7> f;
  let Inst{13-7} = 0b0000001;
  let Inst{6-0} = f;
}
// The two low bits are "don't care"; Microchip's assembler sets them.
def CLRW : Implied<0b00000100000011, "clrw">;

def BCF   : BitOp<0b0100, "bcf">;
def BSF   : BitOp<0b0101, "bsf">;
def BTFSC : BitOp<0b0110, "btfsc">;
def BTFSS : BitOp<0b0111, "btfss">;

def ADDLW : LitOp<0b111110, "addlw">;
def ANDLW : LitOp<0b111001, "andlw">;
def IORLW : LitOp<0b111000, "iorlw">;
def MOVLW : LitOp<0b110000, "movlw">;
def SUBLW : LitOp<0b111100, "sublw">;
def XORLW : LitOp<0b111010, "xorlw">;
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RETLW : LitOp<0b110100, "retlw">;

def MOVLB : PICInst<(outs), (ins bankop:$k), "movlb\t$k"> {
  bits<5> k;
  let Inst{13-5} = 0b000000001;
  let Inst{4-0} = k;
}
def MOVLP : PICInst<(outs), (ins pageop:$k), "movlp\t$k"> {
  bits<7> k;
  let Inst{13-7} = 0b1100011;
  let Inst{6-0} = k;
}

let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
def BRA : PICInst<(outs), (ins bratarget:$k), "bra\t$k"> {
  bits<9> k;
  let Inst{13-9} = 0b11001;
  let Inst{8-0} = k;
}
def GOTO : PICInst<(outs), (ins calltarget:$k), "goto\t$k"> {
  bits<11> k;
  let Inst{13-11} = 0b101;
  let Inst{10-0} = k;
}
let isIndirectBranch = 1 in
def BRW : Implied<0b00000000001011, "brw">;
}

let isCall = 1 in {
def CALL : PICInst<(outs), (ins calltarget:$k), "call\t$k"> {
  bits<11> k;
  let Inst{13-11} = 0b100;
  let Inst{10-0} = k;
}
def CALLW : Implied<0b00000000001010, "callw">;
}

let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
def RETURN : Implied<0b00000000001000, "return">;
def RETFIE : Implied<0b00000000001001, "retfie">;
}

def NOP    : Implied<0b00000000000000, "nop">;
def RESET  : Implied<0b00000000000001, "reset">;
def OPTION : Implied<0b00000001100010, "option">;
def SLEEP  : Implied<0b00000001100011, "sleep">;
def CLRWDT : Implied<0b00000001100100, "clrwdt">;

def TRIS : PICInst<(outs), (ins bit3:$f), "tris\t$f"> {
  bits<3> f;
  let Inst{13-3} = 0b00000001100;
  let Inst{2-0} = f;
}

def ADDFSR : PICInst<(outs), (ins fsrop:$n, off6:$k), "addfsr\t$n, $k"> {
  bit n;
  bits<6> k;
  let Inst{13-7} = 0b1100010;
  let Inst{6} = n;
  let Inst{5-0} = k;
}

// MOVIW/MOVWI with pre/post increment or decrement.
def MOVIWm : PICInst<(outs), (ins indmode:$m), "moviw\t$m"> {
  bits<3> m;
  let Inst{13-3} = 0b00000000010;
  let Inst{2-0} = m;
}
def MOVWIm : PICInst<(outs), (ins indmode:$m), "movwi\t$m"> {
  bits<3> m;
  let Inst{13-3} = 0b00000000011;
  let Inst{2-0} = m;
}
// MOVIW/MOVWI with a constant offset.
def MOVIWk : PICInst<(outs), (ins off6:$k, fsrop:$n), "moviw\t$k[$n]"> {
  bit n;
  bits<6> k;
  let Inst{13-7} = 0b1111110;
  let Inst{6} = n;
  let Inst{5-0} = k;
}
def MOVWIk : PICInst<(outs), (ins off6:$k, fsrop:$n), "movwi\t$k[$n]"> {
  bit n;
  bits<6> k;
  let Inst{13-7} = 0b1111111;
  let Inst{6} = n;
  let Inst{5-0} = k;
}

//===----------------------------------------------------------------------===//
// Code-generator pseudo instructions
//===----------------------------------------------------------------------===//

class Pseudo<dag outs, dag ins, list<dag> pattern>
    : PICInst<outs, ins, "", pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let Size = 0;
}

let Defs = [SP], Uses = [SP] in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                              [(PICcallseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                            [(PICcallseq_end timm:$amt1, timm:$amt2)]>;
}

// Register copies.
def MOV8rr : Pseudo<(outs GPR8:$d), (ins GPR8:$s), []>;
def MOV16rr : Pseudo<(outs GPR16:$d), (ins GPR16:$s), []>;

// Constants and addresses.
let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in {
def LDI8 : Pseudo<(outs GPR8:$d), (ins i8imm:$k), [(set GPR8:$d, imm:$k)]>;
def LDI16 : Pseudo<(outs GPR16:$d), (ins i16imm:$k), [(set GPR16:$d, imm:$k)]>;
}
def : Pat<(i16 (PICwrapper tglobaladdr:$a)), (LDI16 tglobaladdr:$a)>;
def : Pat<(i16 (PICwrapper texternalsym:$a)), (LDI16 texternalsym:$a)>;
def : Pat<(i16 (PICwrapper tblockaddress:$a)), (LDI16 tblockaddress:$a)>;

// Two-operand arithmetic. The result replaces the first operand.
multiclass ArithRR<SDPatternOperator node, bit commutes = 0> {
  let Constraints = "$d = $a", isCommutable = commutes in {
  def 8rr : Pseudo<(outs GPR8:$d), (ins GPR8:$a, GPR8:$b),
                   [(set GPR8:$d, (node GPR8:$a, GPR8:$b))]>;
  def 16rr : Pseudo<(outs GPR16:$d), (ins GPR16:$a, GPR16:$b),
                    [(set GPR16:$d, (node GPR16:$a, GPR16:$b))]>;
  }
}
multiclass ArithRI<SDPatternOperator node> {
  let Constraints = "$d = $a" in {
  def 8ri : Pseudo<(outs GPR8:$d), (ins GPR8:$a, i8imm:$k),
                   [(set GPR8:$d, (node GPR8:$a, imm:$k))]>;
  def 16ri : Pseudo<(outs GPR16:$d), (ins GPR16:$a, i16imm:$k),
                    [(set GPR16:$d, (node GPR16:$a, imm:$k))]>;
  }
}

let Defs = [CARRY] in {
defm ADD : ArithRR<add, 1>, ArithRI<add>;
defm SUB : ArithRR<sub>;
}
let Defs = [CARRY], Uses = [CARRY], Constraints = "$d = $a" in {
def ADC16rr : Pseudo<(outs GPR16:$d), (ins GPR16:$a, GPR16:$b),
                     [(set GPR16:$d, (adde GPR16:$a, GPR16:$b))]>;
def SBC16rr : Pseudo<(outs GPR16:$d), (ins GPR16:$a, GPR16:$b),
                     [(set GPR16:$d, (sube GPR16:$a, GPR16:$b))]>;
}
def : Pat<(addc GPR16:$a, GPR16:$b), (ADD16rr GPR16:$a, GPR16:$b)>;
def : Pat<(addc GPR16:$a, imm:$k), (ADD16ri GPR16:$a, imm:$k)>;
def : Pat<(subc GPR16:$a, GPR16:$b), (SUB16rr GPR16:$a, GPR16:$b)>;

defm AND : ArithRR<and, 1>, ArithRI<and>;
defm OR  : ArithRR<or, 1>, ArithRI<or>;
defm XOR : ArithRR<xor, 1>, ArithRI<xor>;

let Constraints = "$d = $a" in {
let AddedComplexity = 10 in {
def NOT8 : Pseudo<(outs GPR8:$d), (ins GPR8:$a), [(set GPR8:$d, (not GPR8:$a))]>;
def NOT16 : Pseudo<(outs GPR16:$d), (ins GPR16:$a),
                   [(set GPR16:$d, (not GPR16:$a))]>;
}
let Defs = [CARRY] in {
let AddedComplexity = 10 in {
def NEG8 : Pseudo<(outs GPR8:$d), (ins GPR8:$a), [(set GPR8:$d, (ineg GPR8:$a))]>;
def NEG16 : Pseudo<(outs GPR16:$d), (ins GPR16:$a),
                   [(set GPR16:$d, (ineg GPR16:$a))]>;
}
def SHL8 : Pseudo<(outs GPR8:$d), (ins GPR8:$a), [(set GPR8:$d, (PICshl1 GPR8:$a))]>;
def LSR8 : Pseudo<(outs GPR8:$d), (ins GPR8:$a), [(set GPR8:$d, (PIClsr1 GPR8:$a))]>;
def ASR8 : Pseudo<(outs GPR8:$d), (ins GPR8:$a), [(set GPR8:$d, (PICasr1 GPR8:$a))]>;
def SHL16 : Pseudo<(outs GPR16:$d), (ins GPR16:$a),
                   [(set GPR16:$d, (PICshl1 GPR16:$a))]>;
def LSR16 : Pseudo<(outs GPR16:$d), (ins GPR16:$a),
                   [(set GPR16:$d, (PIClsr1 GPR16:$a))]>;
def ASR16 : Pseudo<(outs GPR16:$d), (ins GPR16:$a),
                   [(set GPR16:$d, (PICasr1 GPR16:$a))]>;
}
}

// Shifts by a run-time amount become loops in the custom inserter.
let usesCustomInserter = 1, Defs = [CARRY, TMP0] in {
def SHLV8 : Pseudo<(outs GPR8:$d), (ins GPR8:$a, GPR8:$n),
                   [(set GPR8:$d, (PICshlv GPR8:$a, GPR8:$n))]>;
def LSRV8 : Pseudo<(outs GPR8:$d), (ins GPR8:$a, GPR8:$n),
                   [(set GPR8:$d, (PIClsrv GPR8:$a, GPR8:$n))]>;
def ASRV8 : Pseudo<(outs GPR8:$d), (ins GPR8:$a, GPR8:$n),
                   [(set GPR8:$d, (PICasrv GPR8:$a, GPR8:$n))]>;
def SHLV16 : Pseudo<(outs GPR16:$d), (ins GPR16:$a, GPR8:$n),
                    [(set GPR16:$d, (PICshlv GPR16:$a, GPR8:$n))]>;
def LSRV16 : Pseudo<(outs GPR16:$d), (ins GPR16:$a, GPR8:$n),
                    [(set GPR16:$d, (PIClsrv GPR16:$a, GPR8:$n))]>;
def ASRV16 : Pseudo<(outs GPR16:$d), (ins GPR16:$a, GPR8:$n),
                    [(set GPR16:$d, (PICasrv GPR16:$a, GPR8:$n))]>;
}

// Widening and narrowing.
def ZEXT16 : Pseudo<(outs GPR16:$d), (ins GPR8:$s), [(set GPR16:$d, (zext GPR8:$s))]>;
def SEXT16 : Pseudo<(outs GPR16:$d), (ins GPR8:$s), [(set GPR16:$d, (sext GPR8:$s))]>;
def : Pat<(i16 (anyext GPR8:$s)),
          (INSERT_SUBREG (i16 (IMPLICIT_DEF)), GPR8:$s, sub_lo)>;
def : Pat<(i8 (trunc GPR16:$s)), (EXTRACT_SUBREG GPR16:$s, sub_lo)>;
def : Pat<(sext_inreg GPR16:$s, i8),
          (SEXT16 (i8 (EXTRACT_SUBREG GPR16:$s, sub_lo)))>;
def : Pat<(PICmake16 GPR8:$lo, GPR8:$hi),
          (INSERT_SUBREG (INSERT_SUBREG (i16 (IMPLICIT_DEF)), GPR8:$lo, sub_lo),
                         GPR8:$hi, sub_hi)>;

// Loads and stores. Direct forms are preferred over pointer forms, and
// stack-slot forms over both.
let AddedComplexity = 20 in {
def LD8fi : Pseudo<(outs GPR8:$d), (ins memfi:$a),
                   [(set GPR8:$d, (load addrfi:$a))]>;
def LD16fi : Pseudo<(outs GPR16:$d), (ins memfi:$a),
                    [(set GPR16:$d, (load addrfi:$a))]>;
def ST8fi : Pseudo<(outs), (ins GPR8:$s, memfi:$a),
                   [(store GPR8:$s, addrfi:$a)]>;
def ST16fi : Pseudo<(outs), (ins GPR16:$s, memfi:$a),
                    [(store GPR16:$s, addrfi:$a)]>;
}
let AddedComplexity = 10 in {
def LD8abs : Pseudo<(outs GPR8:$d), (ins memabs:$a),
                    [(set GPR8:$d, (load addrabs:$a))]>;
def LD16abs : Pseudo<(outs GPR16:$d), (ins memabs:$a),
                     [(set GPR16:$d, (load addrabs:$a))]>;
def ST8abs : Pseudo<(outs), (ins GPR8:$s, memabs:$a),
                    [(store GPR8:$s, addrabs:$a)]>;
def ST16abs : Pseudo<(outs), (ins GPR16:$s, memabs:$a),
                     [(store GPR16:$s, addrabs:$a)]>;
}
def LD8ptr : Pseudo<(outs GPR8:$d), (ins memptr:$a),
                    [(set GPR8:$d, (load addrptr8:$a))]>;
def LD16ptr : Pseudo<(outs GPR16:$d), (ins memptr:$a),
                     [(set GPR16:$d, (load addrptr16:$a))]>;
def ST8ptr : Pseudo<(outs), (ins GPR8:$s, memptr:$a),
                    [(store GPR8:$s, addrptr8:$a)]>;
def ST16ptr : Pseudo<(outs), (ins GPR16:$s, memptr:$a),
                     [(store GPR16:$s, addrptr16:$a)]>;

// Read-modify-write of a directly addressed byte: one instruction on the
// file register instead of load, operate, store. The byte is read once and
// written once, as the separate load and store would, so this is also valid
// for volatile objects such as special function registers.
multiclass RMW<SDPatternOperator node> {
  def mi : Pseudo<(outs), (ins memabs:$a, i8imm:$k),
                  [(store (node (i8 (load addrabs:$a)), imm:$k), addrabs:$a)]>;
  def mr : Pseudo<(outs), (ins memabs:$a, GPR8:$s),
                  [(store (node (i8 (load addrabs:$a)), GPR8:$s), addrabs:$a)]>;
}
let AddedComplexity = 30 in {
defm AND8 : RMW<and>;
defm OR8 : RMW<or>;
defm XOR8 : RMW<xor>;
let Defs = [CARRY] in
defm ADD8 : RMW<add>;
}

// Outgoing call arguments, stored at an offset from the stack pointer. The
// call frame is reserved by the prologue, so these are stack-slot stores.
def : Pat<(PICstoresp GPR8:$s, timm:$off), (ST8fi GPR8:$s, timm:$off, 0)>;
def : Pat<(PICstoresp GPR16:$s, timm:$off), (ST16fi GPR16:$s, timm:$off, 0)>;

// Address of a stack slot.
let Defs = [CARRY] in
def LEAfi : Pseudo<(outs GPR16:$d), (ins memfi:$a), []>;

// Control flow. A conditional branch carries its own comparison, so no
// condition flags are live between instructions.
let isBranch = 1, isTerminator = 1 in {
let isBarrier = 1 in
def JMP : Pseudo<(outs), (ins brtarget:$t), [(br bb:$t)]>;
// Branch that cannot reach its target with BRA.
let isBarrier = 1 in
def JMPFAR : Pseudo<(outs), (ins brtarget:$t), []>;
// Jump to the code address in a register pair.
let isBarrier = 1, isIndirectBranch = 1 in
def JMPreg : Pseudo<(outs), (ins GPR16:$t), [(brind GPR16:$t)]>;

let Defs = [CARRY, TMP0] in {
def BRCC8rr : Pseudo<(outs), (ins cc:$c, GPR8:$a, GPR8:$b, brtarget:$t),
                     [(PICbrcc timm:$c, GPR8:$a, GPR8:$b, bb:$t)]>;
def BRCC8ri : Pseudo<(outs), (ins cc:$c, GPR8:$a, i8imm:$k, brtarget:$t),
                     [(PICbrcc timm:$c, GPR8:$a, imm:$k, bb:$t)]>;
def BRCC16rr : Pseudo<(outs), (ins cc:$c, GPR16:$a, GPR16:$b, brtarget:$t),
                      [(PICbrcc timm:$c, GPR16:$a, GPR16:$b, bb:$t)]>;
def BRCC16ri : Pseudo<(outs), (ins cc:$c, GPR16:$a, i16imm:$k, brtarget:$t),
                      [(PICbrcc timm:$c, GPR16:$a, imm:$k, bb:$t)]>;
}
}

// Compare and select, expanded to a branch diamond by the custom inserter.
multiclass Select<RegisterClass VRC, RegisterClass CRC> {
  def NAME : Pseudo<(outs VRC:$d), (ins cc:$c, CRC:$a, CRC:$b, VRC:$t, VRC:$f),
                    [(set VRC:$d, (PICselectcc timm:$c, CRC:$a, CRC:$b,
                                               VRC:$t, VRC:$f))]>;
}
let usesCustomInserter = 1, Defs = [CARRY, TMP0] in {
defm SELECT8c8 : Select<GPR8, GPR8>;
defm SELECT8c16 : Select<GPR8, GPR16>;
defm SELECT16c8 : Select<GPR16, GPR8>;
defm SELECT16c16 : Select<GPR16, GPR16>;
}

let isCall = 1, Uses = [SP] in {
def CALLsym : Pseudo<(outs), (ins i16imm:$t), []>;
def CALLreg : Pseudo<(outs), (ins GPR16:$t), [(PICcall GPR16:$t)]>;
}
def : Pat<(PICcall (i16 tglobaladdr:$t)), (CALLsym tglobaladdr:$t)>;
def : Pat<(PICcall (i16 texternalsym:$t)), (CALLsym texternalsym:$t)>;

let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
def RET : Pseudo<(outs), (ins), [(PICretglue)]>;
def RETI : Pseudo<(outs), (ins), [(PICretiglue)]>;
}
