/* Where the compiler sends a program that reaches code it was told cannot
 * be reached (__builtin_trap, and what C++ puts after a call of a pure
 * virtual function): stop the processor. The C library has its own. */
void abort(void) __attribute__((weak, noreturn));
void abort(void)
{
    for (;;)
        __asm__ volatile("sleep");
}

/* What the C library's abort and exit end in. */
void _exit(int status) __attribute__((weak, noreturn));
void _exit(int status)
{
    (void)status;
    for (;;)
        __asm__ volatile("sleep");
}
