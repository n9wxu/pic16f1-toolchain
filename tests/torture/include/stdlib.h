/* Minimal declarations for conformance tests. */
#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
void abort(void) __attribute__((noreturn));
void exit(int) __attribute__((noreturn));
#endif
