; A function only this module calls may return its byte in W and take a
; byte argument in W, where that makes the functions concerned smaller: a
; result that is loaded last stays in W, and a constant argument is loaded
; and passed in one instruction. A function called through a pointer keeps
; the documented convention.
; RUN: llc -mtriple=pic16f1 -O2 < %s | FileCheck %s
; RUN: llc -mtriple=pic16f1 -O2 -pic16f1-w-conventions=0 < %s | FileCheck %s --check-prefix=OFF

@port = global i8 0
@op = global ptr @open

define internal i8 @get() noinline {
; CHECK-LABEL: get:
; CHECK:      movf port, W
; CHECK-NEXT: return
; OFF-LABEL: get:
; OFF:      movf port, W
; OFF-NEXT: movwf 0x70
; OFF-NEXT: return
  %v = load volatile i8, ptr @port
  ret i8 %v
}

define internal void @put(i8 %c) noinline {
; CHECK-LABEL: put:
; CHECK:      movlb bank(port)
; CHECK-NEXT: movwf port
; CHECK-NEXT: return
; OFF-LABEL: put:
; OFF: movf 0x70, W
  store volatile i8 %c, ptr @port
  ret void
}

define internal i8 @open(i8 %c) noinline {
; CHECK-LABEL: open:
; CHECK:      incf 0x70, F
; CHECK-NEXT: return
  %r = add i8 %c, 1
  ret i8 %r
}

define void @main() {
; CHECK-LABEL: main:
; CHECK:      movlw 62
; CHECK-NEXT: movlp page(put)
; CHECK-NEXT: call put
; CHECK:      call get
; CHECK-NEXT: movwf 0x70
; CHECK-NEXT: xorlw 1
; CHECK:      callw
; CHECK-NEXT: movf 0x70, W
; CHECK-NEXT: movlp page(put)
; CHECK-NEXT: goto put
; OFF-LABEL: main:
; OFF:      movlw 62
; OFF-NEXT: movwf 0x70
; OFF:      call get
; OFF-NEXT: movf 0x70, W
  call void @put(i8 62)
  %k = call i8 @get()
  %z = icmp eq i8 %k, 1
  br i1 %z, label %a, label %b
a:
  call void @put(i8 33)
  br label %b
b:
  %f = load volatile ptr, ptr @op
  %o = call i8 %f(i8 %k)
  call void @put(i8 %o)
  ret void
}
