/* setjmp and longjmp for the PIC16F1 LLVM toolchain. The routines are in
 * the runtime library (toolchain/runtime/setjmp.S), so they are there with
 * or without the C library. Not for leaving an interrupt handler. */
#ifndef __SETJMP_H
#define __SETJMP_H

typedef unsigned char jmp_buf[10];

int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif
