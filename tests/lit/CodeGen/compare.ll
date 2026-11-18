; RUN: llc -O2 < %s | FileCheck %s
target triple = "pic16f1"

; STATUS is file register 0x03; bit 0 is carry, bit 2 is zero.

; Unsigned less-than: subtract and test carry (set means no borrow).
define i8 @ult(i8 %a, i8 %b) {
; CHECK-LABEL: ult:
; CHECK:      movf 0x71, W
; CHECK-NEXT: subwf 0x70, W
; CHECK-NEXT: btfss 0x03, 0
; CHECK-NEXT: bra
  %c = icmp ult i8 %a, %b
  %r = select i1 %c, i8 %a, i8 %b
  ret i8 %r
}

; Equality: exclusive-or and test zero.
define i8 @eq(i8 %a, i8 %b) {
; CHECK-LABEL: eq:
; CHECK:      xorwf 0x7{{[0-9a-d]}}, W
; CHECK-NEXT: bt{{fsc|fss}} 0x03, 2
  %c = icmp eq i8 %a, %b
  %r = select i1 %c, i8 1, i8 7
  ret i8 %r
}

; Signed comparison flips both sign bits and compares unsigned.
define i8 @slt(i8 %a, i8 %b) {
; CHECK-LABEL: slt:
; CHECK:     xorlw 128
; CHECK:     movwf 0x7e
; CHECK:     xorlw 128
; CHECK:     subwf 0x7e, W
; CHECK:     bt{{fsc|fss}} 0x03, 0
  %c = icmp slt i8 %a, %b
  %r = select i1 %c, i8 %a, i8 %b
  ret i8 %r
}

; A branch on the sign needs only a bit test.
declare void @hit()
define void @negative(i16 %a) {
; CHECK-LABEL: negative:
; CHECK: bt{{fsc|fss}} 0x71, 7
entry:
  %c = icmp slt i16 %a, 0
  br i1 %c, label %yes, label %no
yes:
  call void @hit()
  br label %no
no:
  ret void
}
