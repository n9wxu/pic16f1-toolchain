; RUN: llc -O2 < %s | FileCheck %s
target triple = "pic16f1"

declare i16 @ext(i16, i8)
declare void @many(i16, i16, i16, i16, i16)

; Arguments go in R0 up; PCLATH is set before every call.
define i16 @call(i16 %x) {
; CHECK-LABEL: call:
; CHECK:      movlw 3
; CHECK-NEXT: movwf 0x72
; CHECK-NEXT: movlp page(ext)
; CHECK-NEXT: call ext
  %r = call i16 @ext(i16 %x, i8 3)
  ret i16 %r
}

; The fifth 16-bit argument does not fit in R0-R7 and goes on the stack.
define void @stackarg() {
; CHECK-LABEL: stackarg:
; CHECK: addfsr FSR1, -2
; CHECK: movwi 0[FSR1]
; CHECK: movwi 1[FSR1]
; CHECK: call many
; CHECK: addfsr FSR1, 2
  call void @many(i16 1, i16 2, i16 3, i16 4, i16 500)
  ret void
}

; A call through a pointer loads PCLATH and W, then CALLW.
define void @indirect(ptr %f) {
; CHECK-LABEL: indirect:
; CHECK:      movf 0x71, W
; CHECK-NEXT: movwf 0x0a
; CHECK-NEXT: movf 0x70, W
; CHECK-NEXT: callw
  call void %f()
  ret void
}

; The address of a function is its word address.
define ptr @address() {
; CHECK-LABEL: address:
; CHECK: movlw pm_lo8(ext)
; CHECK: movlw pm_hi8(ext)
  ret ptr @ext
}

; R8 is preserved across calls, so a value kept there is saved first.
define i16 @keep(i16 %x) {
; CHECK-LABEL: keep:
; CHECK: addfsr FSR1, -2
; CHECK: movf 0x78, W
; CHECK: movwi 1[FSR1]
; CHECK: call ext
; CHECK: moviw 1[FSR1]
; CHECK: movwf 0x78
; CHECK: addfsr FSR1, 2
  %r = call i16 @ext(i16 %x, i8 0)
  %s = add i16 %r, %x
  ret i16 %s
}
