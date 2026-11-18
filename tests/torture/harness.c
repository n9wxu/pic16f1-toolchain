/* Harness for running third-party C conformance tests (such as the GCC
 * torture tests) on the simulator. exit(n) and a return from main report n
 * as the exit code on PORTD; abort() reports 134. The startup code passes
 * main's result to exit. */
#include <xc.h>

PIC_CONFIG_WORDS(0x0FE4, 0x1EFF);

__attribute__((noreturn)) void exit(int code)
{
    TRISD = 0;
    LATD = (unsigned char)code;
    for (;;) {
        SLEEP();
    }
}

__attribute__((noreturn)) void abort(void)
{
    exit(134);
}

/* Small string routines many tests assume. */
unsigned int strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0) {
    }
    return r;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, unsigned int n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    return n ? (int)(unsigned char)*a - (int)(unsigned char)*b : 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return 0;
    }
}

/* Output is discarded: the tests only use it for diagnostics. */
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int puts(const char *s) { (void)s; return 0; }
int putchar(int c) { return c; }
