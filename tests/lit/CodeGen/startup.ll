; RUN: llc -mcpu=pic16f1937 -O2 < %s | FileCheck %s
target triple = "pic16f1"

; Constants live in flash and need neither startup loop, so a module with
; only constants asks for none.
@k = constant [4 x i8] c"abc\00"

define i8 @first() {
  %c = load i8, ptr @k
  ret i8 %c
}

; CHECK-NOT: __do_copy_data
; CHECK-NOT: __do_clear_bss
