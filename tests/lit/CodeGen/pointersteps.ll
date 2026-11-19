; RUN: llc -mcpu=pic16f1937 -O2 < %s | FileCheck %s
; RUN: llc -mcpu=pic16f1937 -O2 -pic16f1-pointer-ivs=0 < %s | FileCheck %s --check-prefix=INDEX
target triple = "pic16f1"

; An array indexed by a loop counter is walked with a pointer that stays in
; FSR0 and is stepped by the access; the counter is then free to count down
; to zero.

; CHECK-LABEL: fill:
; CHECK:      movlw lo8(a8)
; CHECK-NEXT: movwf 0x04
; CHECK:      movwi FSR0++
; CHECK-NEXT: decfsz
; CHECK-NEXT: bra
; CHECK:      return
; INDEX-LABEL: fill:
; INDEX:      addwfc
; INDEX:      movwi 0[FSR0]

; Two arrays need two pointers, and this function, which another file may
; call, has only FSR0: it keeps the index.
; CHECK-LABEL: copy:
; CHECK:      movlw lo8(src)
; CHECK:      addwf
; CHECK:      movlw lo8(a8)
; CHECK:      addwf
; CHECK:      return

; Neighbouring elements are reached at small offsets from one pointer.
; CHECK-LABEL: check:
; CHECK:      movlw lo8(a8+1)
; CHECK:      addfsr FSR0, 1
; CHECK:      moviw -1[FSR0]
; CHECK:      moviw 0[FSR0]
; CHECK-NOT:  addwfc
; CHECK:      return

@a8 = global [32 x i8] zeroinitializer
@src = global [32 x i8] zeroinitializer

define void @fill(i8 %v) #0 {
entry:
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body
  ret void

for.body:                                         ; preds = %entry, %for.body
  %i.04 = phi i8 [ 0, %entry ], [ %inc, %for.body ]
  %conv = zext nneg i8 %i.04 to i16
  %arrayidx = getelementptr inbounds nuw i8, ptr @a8, i16 %conv
  store i8 %v, ptr %arrayidx
  %inc = add nuw nsw i8 %i.04, 1
  %exitcond.not = icmp eq i8 %inc, 32
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}

define void @copy() #1 {
entry:
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body
  ret void

for.body:                                         ; preds = %entry, %for.body
  %i.07 = phi i8 [ 0, %entry ], [ %inc, %for.body ]
  %conv = zext nneg i8 %i.07 to i16
  %arrayidx = getelementptr inbounds nuw i8, ptr @src, i16 %conv
  %0 = load i8, ptr %arrayidx
  %arrayidx3 = getelementptr inbounds nuw i8, ptr @a8, i16 %conv
  store i8 %0, ptr %arrayidx3
  %inc = add nuw nsw i8 %i.07, 1
  %exitcond.not = icmp eq i8 %inc, 32
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}

define range(i8 0, 2) i8 @check() #0 {
entry:
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body
  ret i8 %spec.select

for.body:                                         ; preds = %entry, %for.body
  %i.012 = phi i8 [ 1, %entry ], [ %inc, %for.body ]
  %ok.011 = phi i8 [ 1, %entry ], [ %spec.select, %for.body ]
  %conv = zext nneg i8 %i.012 to i16
  %0 = getelementptr i8, ptr @a8, i16 %conv
  %arrayidx = getelementptr i8, ptr %0, i16 -1
  %1 = load i8, ptr %arrayidx
  %2 = load i8, ptr %0
  %cmp6 = icmp ugt i8 %1, %2
  %spec.select = select i1 %cmp6, i8 0, i8 %ok.011
  %inc = add nuw nsw i8 %i.012, 1
  %exitcond.not = icmp eq i8 %inc, 32
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}
