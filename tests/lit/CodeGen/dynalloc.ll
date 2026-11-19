; RUN: llc -mcpu=pic16f1937 -O2 < %s | FileCheck %s
target triple = "pic16f1"

; A variable-sized object is taken from the stack by lowering the cell __sp.
; FSR1 stays at the fixed part of the frame; the function keeps a copy of
; it in 0x7C/0x7D (saving what was there) and takes it back after a call.

declare void @use(ptr)

define i8 @vla(i16 %n) {
; CHECK-LABEL: vla:
; The old value of the pair is pushed, then the frame base goes in it.
; CHECK:       movf 0x7c, W
; CHECK-NEXT:  movwi --FSR1
; CHECK:       movf 0x06, W
; CHECK-NEXT:  movwf 0x7c
; CHECK-NEXT:  movf 0x07, W
; CHECK-NEXT:  movwf 0x7d
; The allocation.
; CHECK:       subwf __sp, F
; CHECK:       subwfb __sp+1, F
; CHECK:       call use
; The frame base comes back for the epilogue, which then gives the entry
; stack pointer back to the cell and with it the object.
; CHECK:       movf 0x7c, W
; CHECK-NEXT:  movwf 0x06
; CHECK-NEXT:  movf 0x7d, W
; CHECK-NEXT:  movwf 0x07
; CHECK:       movwf __sp+1
; CHECK:       return
  %a = alloca i8, i16 %n
  call void @use(ptr %a)
  %v = load i8, ptr %a
  ret i8 %v
}

; Leaving a block gives its objects back: the cell is saved and restored.
define void @scoped(i16 %n) {
; CHECK-LABEL: scoped:
; CHECK:       movf __sp, W
; CHECK:       subwf __sp, F
; CHECK:       call use
; CHECK:       movwf __sp
; CHECK:       return
  %sp = call ptr @llvm.stacksave.p0()
  %a = alloca i8, i16 %n
  call void @use(ptr %a)
  call void @llvm.stackrestore.p0(ptr %sp)
  ret void
}

declare ptr @llvm.stacksave.p0()
declare void @llvm.stackrestore.p0(ptr)
