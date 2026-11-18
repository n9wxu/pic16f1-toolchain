; RUN: llc -mcpu=pic16f1937 -O2 < %s | FileCheck %s
; RUN: llc -mcpu=pic16f1937 -O2 -pic16f1-main-saves-registers < %s | FileCheck %s --check-prefix=SAVE
target triple = "pic16f1"

; The startup code keeps nothing in registers across its call to main, so
; main does not save the callee-saved registers (0x78 and up) unless asked.

declare i8 @next()

define i16 @main() {
; CHECK-LABEL: main:
; CHECK-NOT:   movwi
; CHECK:       call next
; CHECK-NOT:   moviw
; CHECK:       return
; SAVE-LABEL: main:
; SAVE:       movf 0x78, W
; SAVE:       movwi
; SAVE:       call next
; SAVE:       moviw
; SAVE:       movwf 0x78
; SAVE:       return
entry:
  %a = call i8 @next()
  %b = call i8 @next()
  %c = add i8 %a, %b
  %r = zext i8 %c to i16
  ret i16 %r
}
