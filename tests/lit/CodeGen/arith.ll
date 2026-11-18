; RUN: llc -O2 < %s | FileCheck %s
target triple = "pic16f1"

; R0-R13 are file registers 0x70-0x7D. Arguments arrive in R0 up.

; 16-bit add: low bytes, then high bytes with carry.
define i16 @add16(i16 %a, i16 %b) {
; CHECK-LABEL: add16:
; CHECK:      movf 0x72, W
; CHECK-NEXT: addwf 0x70, F
; CHECK-NEXT: movf 0x73, W
; CHECK-NEXT: addwfc 0x71, F
; CHECK-NEXT: return
  %r = add i16 %a, %b
  ret i16 %r
}

; 32-bit add: the carry runs through all four bytes.
define i32 @add32(i32 %a, i32 %b) {
; CHECK-LABEL: add32:
; CHECK:      movf 0x74, W
; CHECK-NEXT: addwf 0x70, F
; CHECK-NEXT: movf 0x75, W
; CHECK-NEXT: addwfc 0x71, F
; CHECK-NEXT: movf 0x76, W
; CHECK-NEXT: addwfc 0x72, F
; CHECK-NEXT: movf 0x77, W
; CHECK-NEXT: addwfc 0x73, F
; CHECK-NEXT: return
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @sub32(i32 %a, i32 %b) {
; CHECK-LABEL: sub32:
; CHECK:      subwf 0x70, F
; CHECK:      subwfb 0x71, F
; CHECK:      subwfb 0x72, F
; CHECK:      subwfb 0x73, F
  %r = sub i32 %a, %b
  ret i32 %r
}

; A 16-bit shift by nine is a byte move and a one-bit shift.
define i16 @shl9(i16 %x) {
; CHECK-LABEL: shl9:
; CHECK:     lslf 0x70, F
; CHECK-NOT: lslf
; CHECK-NOT: rlf
; CHECK:     return
  %r = shl i16 %x, 9
  ret i16 %r
}

; There is no multiply instruction.
define i16 @mul16(i16 %a, i16 %b) {
; CHECK-LABEL: mul16:
; CHECK: movlp page(__mulhi3)
; CHECK: call __mulhi3
  %r = mul i16 %a, %b
  ret i16 %r
}

; A 32-bit shift by five calls the shift routine rather than expanding.
define i32 @shl5(i32 %x) {
; CHECK-LABEL: shl5:
; CHECK: call __ashlsi3
  %r = shl i32 %x, 5
  ret i32 %r
}
