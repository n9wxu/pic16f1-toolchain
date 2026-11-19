; RUN: llc -O2 < %s | FileCheck %s
target triple = "pic16f1"

@b = global i8 0
declare void @work()

; An interrupt handler goes in .isr and returns with RETFIE. One that
; touches no byte register saves none, and one that uses no stack leaves
; the stack pointer alone.
define void @isr() "interrupt" {
; CHECK:      .section .isr,"ax",@progbits
; CHECK-LABEL: isr:
; CHECK-NOT:  __isr_stack_top
; CHECK:      movlb bank(b)
; CHECK-NEXT: incf b, F
; CHECK-NEXT: retfie
  %v = load i8, ptr @b
  %i = add i8 %v, 1
  store i8 %i, ptr @b
  ret void
}

; A handler that calls a function switches to its own stack and must save
; every register the callee may change: R0-R7 and the two scratch bytes at
; 0x7E and 0x7F. (FSR0 comes back from the shadow registers.)
define void @isr_calls() "interrupt" {
; CHECK-LABEL: isr_calls:
; CHECK:      movlw lo8(__isr_stack_top)
; CHECK-NEXT: movwf 0x06
; CHECK-NEXT: movlw hi8(__isr_stack_top)
; CHECK-NEXT: movwf 0x07
; CHECK-NEXT: movf 0x70, W
; CHECK-NEXT: movwi --FSR1
; CHECK:      movf 0x77, W
; CHECK-NEXT: movwi --FSR1
; CHECK-NEXT: movf 0x7e, W
; CHECK-NEXT: movwi --FSR1
; CHECK-NEXT: movf 0x7f, W
; CHECK-NEXT: movwi --FSR1
; CHECK:      call work
; CHECK-NEXT: moviw FSR1++
; CHECK-NEXT: movwf 0x7f
; CHECK:      movwf 0x70
; CHECK-NEXT: retfie
  call void @work()
  ret void
}

; A 16-bit variable is changed in place, without a register to save.
@cnt = global i16 0
define void @isr_count() "interrupt" {
; CHECK-LABEL: isr_count:
; CHECK:      movlw 3
; CHECK-NEXT: movlb bank(cnt)
; CHECK-NEXT: addwf cnt, F
; CHECK-NEXT: movlw 0
; CHECK-NEXT: addwfc cnt+1, F
; CHECK-NEXT: retfie
  %v = load volatile i16, ptr @cnt
  %a = add i16 %v, 3
  store volatile i16 %a, ptr @cnt
  ret void
}
