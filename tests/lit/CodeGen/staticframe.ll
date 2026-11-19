; RUN: llc -mcpu=pic16f1937 -O2 -enable-machine-outliner=never < %s | FileCheck %s
; RUN: llc -mcpu=pic16f1937 -O2 -enable-machine-outliner=never -pic16f1-static-frames=0 < %s | FileCheck %s --check-prefix=STACK
; RUN: llc -mcpu=pic16f1937 -O2 -enable-machine-outliner=never -pic16f1-fsr1-pointer=0 < %s | FileCheck %s --check-prefix=ONE

; Static frames. A function that cannot be entered while it is running keeps
; its variables at fixed addresses and never touches the stack, so FSR1,
; elsewhere the stack pointer, is a second pointer register in it.

; visible can be called from another file, where it might run in an
; interrupt handler. This version has its array on the stack, with the
; stack pointer taken from the cell __sp; the linker may replace it by
; visible.sf, which has a static frame.
; CHECK-LABEL: visible:
; CHECK:      movf __sp, W
; CHECK:      addfsr FSR1, -4
; CHECK:      movwi 0[FSR1]
; CHECK:      addfsr FSR1, 4

; local is called only from main: its array has a fixed address.
; CHECK-LABEL: local:
; CHECK-NOT:  addfsr
; CHECK-NOT:  __sp
; CHECK:      movlb bank(local.frame)
; CHECK:      movwf local.frame
; CHECK:      movlw lo8(local.frame)
; CHECK:      movf local.frame+1, W
; CHECK:      return
; STACK-LABEL: local:
; STACK:      addfsr FSR1, -4

; recurse calls itself: stack.
; CHECK-LABEL: recurse:
; CHECK:      addfsr FSR1, -4
; CHECK:      call recurse

; cmp steps through two strings, with one pointer in FSR0 and the other in
; FSR1. Nothing has to be moved out of FSR1 first.
; CHECK-LABEL: cmp:
; CHECK-NOT:  __sp
; CHECK:      movwf 0x05
; CHECK:      movwf 0x07
; CHECK:      moviw 0[FSR1]
; CHECK:      addfsr FSR1, 1
; CHECK-NEXT: moviw ++FSR0
; CHECK:      return
; ONE-LABEL: cmp:
; ONE-NOT:    FSR1
; ONE:        return

; The static version of visible.
; CHECK-LABEL: visible.sf:
; CHECK-NOT:  FSR1
; CHECK:      movwf visible.sf.frame
; CHECK:      return

; The fixed variables are not cleared at startup and take no room in the
; file.
; CHECK:      .section .noinit.local.frame,"aw",@nobits
; CHECK-NOT:  __do_clear_bss

target triple = "pic16f1"
@sink = external global i8
@s1 = external global ptr
@s2 = external global ptr
define i8 @visible(i8 %x) {
entry:
  %v = alloca [4 x i8]
  call void @llvm.lifetime.start.p0(ptr %v)
  store i8 %x, ptr %v
  call void @use(ptr %v)
  %arrayidx1 = getelementptr inbounds nuw i8, ptr %v, i16 1
  %0 = load i8, ptr %arrayidx1
  call void @llvm.lifetime.end.p0(ptr %v)
  ret i8 %0
}
declare void @llvm.lifetime.start.p0(ptr captures(none))
declare void @use(ptr)
declare void @llvm.lifetime.end.p0(ptr captures(none))
define void @main() {
entry:
  %call = tail call fastcc i8 @local()
  store i8 %call, ptr @sink
  tail call fastcc void @recurse(i8 2)
  store i8 0, ptr @sink
  %0 = load ptr, ptr @s1
  %1 = load ptr, ptr @s2
  %call2 = tail call fastcc i8 @cmp(ptr %0, ptr %1)
  store i8 %call2, ptr @sink
  ret void
}
define internal fastcc i8 @local() {
entry:
  %v = alloca [4 x i8]
  call void @llvm.lifetime.start.p0(ptr %v)
  store i8 1, ptr %v
  call void @use(ptr %v)
  %arrayidx1 = getelementptr inbounds nuw i8, ptr %v, i16 1
  %0 = load i8, ptr %arrayidx1
  call void @llvm.lifetime.end.p0(ptr %v)
  ret i8 %0
}
define internal fastcc void @recurse(i8 %x) {
entry:
  %v = alloca [4 x i8]
  call void @llvm.lifetime.start.p0(ptr %v)
  store i8 %x, ptr %v
  call void @use(ptr %v)
  %tobool.not = icmp eq i8 %x, 0
  br i1 %tobool.not, label %cond.end, label %cond.true
cond.true:                                        ; preds = %entry
  %arrayidx1 = getelementptr inbounds nuw i8, ptr %v, i16 1
  %0 = load i8, ptr %arrayidx1
  call fastcc void @recurse(i8 %0)
  br label %cond.end
cond.end:                                         ; preds = %entry, %cond.true
  call void @llvm.lifetime.end.p0(ptr %v)
  ret void
}
define internal fastcc i8 @cmp(ptr nofree readonly captures(none) %a, ptr nofree readonly captures(none) %b) {
entry:
  %0 = load i8, ptr %a
  %tobool.not13 = icmp eq i8 %0, 0
  br i1 %tobool.not13, label %while.end, label %land.rhs
land.rhs:                                         ; preds = %entry, %while.body
  %1 = phi i8 [ %3, %while.body ], [ %0, %entry ]
  %b.addr.015 = phi ptr [ %incdec.ptr4, %while.body ], [ %b, %entry ]
  %a.addr.014 = phi ptr [ %incdec.ptr, %while.body ], [ %a, %entry ]
  %2 = load i8, ptr %b.addr.015
  %cmp = icmp eq i8 %1, %2
  br i1 %cmp, label %while.body, label %while.end
while.body:                                       ; preds = %land.rhs
  %incdec.ptr = getelementptr inbounds nuw i8, ptr %a.addr.014, i16 1
  %incdec.ptr4 = getelementptr inbounds nuw i8, ptr %b.addr.015, i16 1
  %3 = load i8, ptr %incdec.ptr
  %tobool.not = icmp eq i8 %3, 0
  br i1 %tobool.not, label %while.end, label %land.rhs
while.end:                                        ; preds = %land.rhs, %while.body, %entry
  %b.addr.0.lcssa = phi ptr [ %b, %entry ], [ %incdec.ptr4, %while.body ], [ %b.addr.015, %land.rhs ]
  %.lcssa = phi i8 [ 0, %entry ], [ 0, %while.body ], [ %1, %land.rhs ]
  %4 = load i8, ptr %b.addr.0.lcssa
  %sub = sub i8 %.lcssa, %4
  ret i8 %sub
}
