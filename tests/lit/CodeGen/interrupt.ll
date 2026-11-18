; RUN: llc -O2 < %s | FileCheck %s
target triple = "pic16f1"

@b = global i8 0
declare void @work()

; An interrupt handler goes in .isr, switches to its own stack, and returns
; with RETFIE. One that touches no byte register saves none.
define void @isr() "interrupt" {
; CHECK:      .section .isr,"ax",@progbits
; CHECK-LABEL: isr:
; CHECK:      movlw lo8(__isr_stack_top)
; CHECK-NEXT: movwf 0x06
; CHECK-NEXT: movlw hi8(__isr_stack_top)
; CHECK-NEXT: movwf 0x07
; CHECK-NEXT: movlb bank(b)
; CHECK-NEXT: incf b, F
; CHECK-NEXT: retfie
  %v = load i8, ptr @b
  %i = add i8 %v, 1
  store i8 %i, ptr @b
  ret void
}

; A handler that calls a function must save every register the callee may
; change: R0-R7 and the two scratch bytes at 0x7E and 0x7F.
define void @isr_calls() "interrupt" {
; CHECK-LABEL: isr_calls:
; CHECK-DAG: movf 0x70, W
; CHECK-DAG: movf 0x77, W
; CHECK-DAG: movf 0x7e, W
; CHECK-DAG: movf 0x7f, W
; CHECK:     call work
; CHECK:     retfie
  call void @work()
  ret void
}
