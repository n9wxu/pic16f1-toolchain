; RUN: llc -O2 < %s | FileCheck %s
target triple = "pic16f1"

@g = global i16 5
@k = constant [4 x i8] c"abc\00"

; A writable global is in RAM: select its bank, then address it directly.
; The bank is selected once: the linker keeps a variable of up to 16 bytes
; within one bank.
define void @store(i16 %v) {
; CHECK-LABEL: store:
; CHECK:      movf 0x70, W
; CHECK-NEXT: movlb bank(g)
; CHECK-NEXT: movwf g
; CHECK-NEXT: movf 0x71, W
; CHECK-NEXT: movwf g+1
  store i16 %v, ptr @g
  ret void
}

; A constant is in flash and is read through FSR0 (0x04, 0x05).
define i8 @flash(i16 %i) {
; CHECK-LABEL: flash:
; CHECK:     movlw lo8(k)
; CHECK:     movlw hi8(k)
; CHECK-NOT: movlb
; CHECK:     movwf 0x04
; CHECK:     movwf 0x05
; CHECK:     moviw 0[FSR0]
  %p = getelementptr [4 x i8], ptr @k, i16 0, i16 %i
  %v = load i8, ptr %p
  ret i8 %v
}

; Setting one bit of a register at a constant address is a single BSF, and
; the volatile register is still read once and written once. 269 is 0x10D:
; bank 2, file address 0x0D.
define void @setbit() {
; CHECK-LABEL: setbit:
; CHECK:      movlb 2
; CHECK-NEXT: bsf 0x0d, 3
; CHECK-NEXT: return
  %v = load volatile i8, ptr inttoptr (i16 269 to ptr)
  %o = or i8 %v, 8
  store volatile i8 %o, ptr inttoptr (i16 269 to ptr)
  ret void
}

define void @clearbit() {
; CHECK-LABEL: clearbit:
; CHECK:      movlb 2
; CHECK-NEXT: bcf 0x0d, 3
  %v = load volatile i8, ptr inttoptr (i16 269 to ptr)
  %o = and i8 %v, -9
  store volatile i8 %o, ptr inttoptr (i16 269 to ptr)
  ret void
}

; Two loads through the same pointer set FSR0 up once.
define i16 @twice(ptr %p) {
; CHECK-LABEL: twice:
; CHECK:     movwf 0x04
; CHECK:     movwf 0x05
; CHECK:     moviw 0[FSR0]
; CHECK-NOT: movwf 0x04
; CHECK:     moviw 1[FSR0]
  %v = load i16, ptr %p
  ret i16 %v
}
