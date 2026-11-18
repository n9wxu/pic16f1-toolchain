; RUN: llc -O2 < %s | FileCheck %s
target triple = "pic16f1"

declare i16 @ext(i16, i8)

; MOVIW/MOVWI reach offsets -32..31 from FSR1. A slot further away is reached
; by moving FSR1 and moving it back. The interrupt handler has its own stack,
; so nothing can write under the moved pointer.
define i16 @frame() {
; CHECK-LABEL: frame:
; CHECK:      addfsr FSR1, -32
; CHECK-NEXT: addfsr FSR1, -8
; CHECK:      addfsr FSR1, 31
; CHECK-NEXT: movwi 4[FSR1]
; CHECK-NEXT: addfsr FSR1, -31
; CHECK:      call ext
; CHECK:      addfsr FSR1, 31
; CHECK-NEXT: moviw 4[FSR1]
; CHECK:      addfsr FSR1, -31
; CHECK:      return
  %a = alloca [40 x i8]
  %p = getelementptr [40 x i8], ptr %a, i16 0, i16 35
  store i8 7, ptr %p
  %r = call i16 @ext(i16 0, i8 0)
  %v = load i8, ptr %p
  %z = zext i8 %v to i16
  ret i16 %z
}

; The address of a local is FSR1 (0x06, 0x07) plus its offset. The second
; array sits ten bytes above the first.
declare void @two(ptr, ptr)
define void @addr() {
; CHECK-LABEL: addr:
; CHECK-DAG: movf 0x06, W
; CHECK-DAG: addlw 10
; CHECK-DAG: addwfc 0x07, W
; CHECK:     call two
  %a = alloca [10 x i8]
  %b = alloca [10 x i8]
  call void @two(ptr %a, ptr %b)
  ret void
}
