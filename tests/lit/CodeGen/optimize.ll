; RUN: llc -mcpu=pic16f1937 -O2 < %s | FileCheck %s
; RUN: llc -mcpu=pic16f1937 -O2 -code-model=small < %s | FileCheck %s --check-prefix=SMALL
target triple = "pic16f1"

; What the passes after register allocation do: the peephole pass over the
; final instructions, register usage across calls, the one-page code model
; and the startup pieces a module asks for.

@g = global i16 0
@big = global [40 x i8] zeroinitializer
@k = constant [4 x i8] c"abc\00"
@v = global i8 5

declare void @sink(i16)

; Load a device register, keep it in a byte register, test a bit of that:
; one bit test of the device register, which is still read exactly once.
define void @wait() {
; CHECK-LABEL: wait:
; CHECK:      movlb 0
; CHECK-NEXT: btfss 0x11, 4
; CHECK-NEXT: bra
; CHECK:      return
entry:
  br label %loop
loop:
  %s = load volatile i8, ptr inttoptr (i16 17 to ptr)
  %b = and i8 %s, 16
  %z = icmp eq i8 %b, 0
  br i1 %z, label %loop, label %done
done:
  ret void
}

; An operation on a fresh copy works on the original, with the result
; through W: no copy first.
define i16 @addk(i16 %x, i16 %y) {
; CHECK-LABEL: addk:
; CHECK:      movlw 232
; CHECK-NEXT: addwf 0x72, W
; CHECK-NEXT: movwf 0x78
; CHECK-NEXT: movlw 3
; CHECK-NEXT: addwfc 0x73, W
; CHECK-NEXT: movwf 0x79
  %a = add i16 %y, 1000
  call void @sink(i16 %x)
  ret i16 %a
}

; The high half of a 16-bit increment follows a skip, so it may or may not
; run and must stay one instruction: only the low half gets that treatment.
define i16 @next(i16 %x, i16 %y) {
; CHECK-LABEL: next:
; CHECK:      incf 0x72, W
; CHECK-NEXT: movwf 0x78
; CHECK-NEXT: btfsc 0x03, 2
; CHECK-NEXT: incf 0x79, F
; CHECK-NEXT: movlp page(sink)
  %a = add i16 %y, 1
  call void @sink(i16 %x)
  ret i16 %a
}

; The caller knows @leaf changes only its argument register, so %b stays
; where it arrived (0x71) across the call and nothing is saved.
define internal i8 @leaf(i8 %x) noinline {
  %r = xor i8 %x, 85
  ret i8 %r
}
define i8 @caller(i8 %a, i8 %b) {
; CHECK-LABEL: caller:
; CHECK:      movlp page(leaf)
; CHECK-NEXT: call leaf
; CHECK-NEXT: movf 0x71, W
; CHECK-NEXT: addwf 0x70, F
; CHECK-NEXT: return
;
; In the one-page code model PCLATH is never set.
; SMALL-LABEL: caller:
; SMALL-NOT:  movlp
; SMALL:      call leaf
  %r = call i8 @leaf(i8 %a)
  %s = add i8 %r, %b
  ret i8 %s
}

; One bank selection serves both bytes of a variable.
define void @bump() {
; CHECK-LABEL: bump:
; CHECK:     movlb bank(g)
; CHECK-NOT: movlb
; CHECK:     return
  %x = load i16, ptr @g
  %y = add i16 %x, 3
  store i16 %y, ptr @g
  ret void
}

; The module has initialised and zeroed data, so it asks the startup code
; for both loops. The one-page model also asks the linker for its check.
; CHECK-NOT: __pic16f1_one_page
; CHECK-DAG: .globl __do_copy_data
; CHECK-DAG: .globl __do_clear_bss
; SMALL-DAG: .globl __pic16f1_one_page
