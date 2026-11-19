; RUN: llc -mcpu=pic16f1937 -O2 -pic16f1-dual-frames=0 < %s | FileCheck %s
; RUN: llc -mcpu=pic16f1937 -O2 -pic16f1-dual-frames=0 -code-model=small < %s | FileCheck %s --check-prefix=SMALL
; RUN: llc -mcpu=pic16f1937 -O2 -pic16f1-dual-frames=0 -enable-machine-outliner=never < %s | FileCheck %s --check-prefix=NEVER
target triple = "pic16f1"

; What is done for size beyond the instruction-by-instruction clean-up:
; pointers kept in FSR0, and instruction sequences that occur more than once
; made into subroutines (in functions optimized for size).

declare void @put(i8)
declare void @use(i16)

; A pointer that steps through a string lives in FSR0: the access steps it,
; and nothing copies it there for each access.
define i8 @sum(ptr %s) {
; CHECK-LABEL: sum:
; CHECK:      movf 0x70, W
; CHECK-NEXT: movwf 0x04
; CHECK-NEXT: movf 0x71, W
; CHECK-NEXT: movwf 0x05
; CHECK:      moviw 0[FSR0]
; CHECK:      moviw ++FSR0
; CHECK-NOT:  movwf 0x04
; CHECK:      return
entry:
  br label %loop
loop:
  %p = phi ptr [ %s, %entry ], [ %next, %body ]
  %acc = phi i8 [ 0, %entry ], [ %add, %body ]
  %c = load i8, ptr %p
  %z = icmp eq i8 %c, 0
  br i1 %z, label %done, label %body
body:
  %add = add i8 %acc, %c
  %next = getelementptr i8, ptr %p, i16 1
  br label %loop
done:
  ret i8 %acc
}

; The same instructions in two functions become one subroutine. A call to it
; needs PCLATH set, except in the one-page code model.
define void @first(i16 %x, i16 %y) optsize {
; CHECK-LABEL: first:
; CHECK:      movlp page(OUTLINED_FUNCTION_0)
; CHECK-NEXT: call OUTLINED_FUNCTION_0
; SMALL-LABEL: first:
; SMALL-NOT:  movlp
; SMALL:      call OUTLINED_FUNCTION_0
; NEVER-LABEL: first:
; NEVER-NOT:  OUTLINED
; NEVER:      goto put
  %a = add i16 %x, 1234
  %b = xor i16 %a, %y
  %c = add i16 %b, 4321
  call void @use(i16 %c)
  call void @put(i8 1)
  ret void
}

define void @second(i16 %x, i16 %y) optsize {
; CHECK-LABEL: second:
; CHECK:      call OUTLINED_FUNCTION_0
; SMALL-LABEL: second:
; SMALL:      call OUTLINED_FUNCTION_0
  %a = add i16 %x, 1234
  %b = xor i16 %a, %y
  %c = add i16 %b, 4321
  call void @use(i16 %c)
  call void @put(i8 2)
  ret void
}

; Without optsize nothing is outlined: each use costs a call and a return.
define void @third(i16 %x, i16 %y) {
; CHECK-LABEL: third:
; CHECK-NOT:  OUTLINED
; CHECK:      goto put
  %a = add i16 %x, 1234
  %b = xor i16 %a, %y
  %c = add i16 %b, 4321
  call void @use(i16 %c)
  call void @put(i8 3)
  ret void
}

; The outlined sequence ends in a call, which is a jump there: the callee
; returns to whoever called the sequence.
; CHECK-LABEL: OUTLINED_FUNCTION_0:
; CHECK:       addwf 0x70, F
; CHECK:       movlp page(use)
; CHECK-NEXT:  goto use
; CHECK-NOT:   return
