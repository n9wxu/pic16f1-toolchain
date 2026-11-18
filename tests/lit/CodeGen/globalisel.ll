; RUN: llc -mcpu=pic16f1937 -global-isel-abort=1 < %s | FileCheck %s
; RUN: llc -mcpu=pic16f1937 -O0 -global-isel-abort=1 < %s | FileCheck %s --check-prefix=O0
target triple = "pic16f1"

; GlobalISel is the selector. With -global-isel-abort=1 a function it cannot
; handle is an error instead of a silent fall back to SelectionDAG, so every
; function here also checks that no fall back happens.

; A one-bit shift of a wide value moves through the carry flag.
define i32 @shl1(i32 %x) {
; CHECK-LABEL: shl1:
; CHECK:      lslf 0x70, F
; CHECK-NEXT: rlf 0x71, F
; CHECK-NEXT: rlf 0x72, F
; CHECK-NEXT: rlf 0x73, F
; CHECK-NEXT: return
; O0-LABEL: shl1:
  %r = shl i32 %x, 1
  ret i32 %r
}

define i32 @lshr1(i32 %x) {
; CHECK-LABEL: lshr1:
; CHECK:      lsrf 0x73, F
; CHECK-NEXT: rrf 0x72, F
; CHECK-NEXT: rrf 0x71, F
; CHECK-NEXT: rrf 0x70, F
; CHECK-NEXT: return
; O0-LABEL: lshr1:
  %r = lshr i32 %x, 1
  ret i32 %r
}

define i32 @ashr1(i32 %x) {
; CHECK-LABEL: ashr1:
; CHECK:      asrf 0x73, F
; CHECK-NEXT: rrf 0x72, F
; CHECK-NEXT: rrf 0x71, F
; CHECK-NEXT: rrf 0x70, F
; CHECK-NEXT: return
; O0-LABEL: ashr1:
  %r = ashr i32 %x, 1
  ret i32 %r
}

; A 32-bit add is one chain of add-with-carry.
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
; O0-LABEL: add32:
  %r = add i32 %a, %b
  ret i32 %r
}

; An unsigned order of wide values is the borrow of a subtract chain.
; STATUS is file register 0x03; bit 0 is carry, clear after a borrow.
define i8 @ult32(i32 %a, i32 %b) {
; CHECK-LABEL: ult32:
; CHECK:      subwf 0x70, F
; CHECK:      subwfb 0x71, F
; CHECK:      subwfb 0x72, F
; CHECK:      subwfb 0x73, F
; CHECK-NEXT: clrf 0x70
; CHECK-NEXT: btfss 0x03, 0
; CHECK-NEXT: incf 0x70, F
; CHECK-NEXT: return
; O0-LABEL: ult32:
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

; The sign of a wide value is one bit of its top byte.
define i8 @neg32(i32 %a) {
; CHECK-LABEL: neg32:
; CHECK:      btfsc 0x73, 7
; CHECK-NOT:  subwf
; CHECK:      return
; O0-LABEL: neg32:
  %c = icmp slt i32 %a, 0
  %r = zext i1 %c to i8
  ret i8 %r
}

; Floating point and narrow division are runtime calls.
define float @fadd(float %a, float %b) {
; CHECK-LABEL: fadd:
; CHECK: call __addsf3
; O0-LABEL: fadd:
; O0: call __addsf3
  %r = fadd float %a, %b
  ret float %r
}

define i8 @fcmp(float %a, float %b) {
; CHECK-LABEL: fcmp:
; CHECK: call __ltsf2
; O0-LABEL: fcmp:
  %c = fcmp olt float %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @udiv8(i8 %a, i8 %b) {
; CHECK-LABEL: udiv8:
; CHECK: call __udivqi3
; O0-LABEL: udiv8:
; O0: call __udivqi3
  %r = udiv i8 %a, %b
  ret i8 %r
}

; Division by a constant stays a division.
define i16 @div10(i16 %a) {
; CHECK-LABEL: div10:
; CHECK: call __udivhi3
; O0-LABEL: div10:
  %r = udiv i16 %a, 10
  ret i16 %r
}

; Two generic combine rules turn and(trunc x, trunc y) into trunc(and x, y)
; and back. Together they never finished, and used memory without bound.
define i1 @and_of_truncs(i16 %x, i16 %y) {
; CHECK-LABEL: and_of_truncs:
; CHECK: return
; O0-LABEL: and_of_truncs:
  %a = trunc i16 %x to i1
  %b = trunc i16 %y to i1
  %r = and i1 %a, %b
  ret i1 %r
}

; A constant stored through a pointer may change what a global holds, so the
; load of @g stays above the store.
@g = global i16 0
@gp = global ptr @g
@gd = global i16 0
define void @load_before_store(i16 %c) {
; CHECK-LABEL: load_before_store:
; CHECK: movf g, W
; CHECK: movwi 0[FSR0]
; CHECK: return
; O0-LABEL: load_before_store:
  %v = load i16, ptr @g
  %p = load ptr, ptr @gp
  store i16 1, ptr %p
  %z = icmp eq i16 %c, 0
  br i1 %z, label %then, label %end
then:
  store i16 %v, ptr @gd
  br label %end
end:
  ret void
}

; A debug value for a variable wider than a register does not stop
; selection.
declare i16 @take(i32)
define i16 @wide_debug(i32 %v) !dbg !4 {
; CHECK-LABEL: wide_debug:
; CHECK: call take
; O0-LABEL: wide_debug:
    #dbg_value(i32 %v, !8, !DIExpression(), !10)
  %r = call i16 @take(i32 %v), !dbg !10
  ret i16 %r, !dbg !10
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}
!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "t.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 7, !"Dwarf Version", i32 4}
!4 = distinct !DISubprogram(name: "wide_debug", scope: !1, file: !1, line: 1, type: !5, unit: !0, retainedNodes: !7)
!5 = !DISubroutineType(types: !6)
!6 = !{}
!7 = !{!8}
!8 = !DILocalVariable(name: "v", arg: 1, scope: !4, file: !1, line: 1, type: !9)
!9 = !DIBasicType(name: "long", size: 32, encoding: DW_ATE_signed)
!10 = !DILocation(line: 1, scope: !4)
