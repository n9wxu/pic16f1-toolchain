/* What compiled C++ refers to besides its own functions (the Itanium C++
 * ABI's support routines), as far as this toolchain supports the language:
 * no exceptions and no run-time type information. */

/* A call of a pure virtual function, which only an object under
 * construction or destruction can make. There is nowhere to report it. */
void __cxa_pure_virtual(void)
{
    for (;;)
        __asm__ volatile("sleep");
}

/* The same for a deleted virtual function. */
void __cxa_deleted_virtual(void)
{
    for (;;)
        __asm__ volatile("sleep");
}

/* Registration of a destructor to run at exit, for a program compiled
 * with static destructors turned back on: accepted and never run. */
int atexit(void (*function)(void)) __attribute__((weak));
int atexit(void (*function)(void))
{
    (void)function;
    return 0;
}

/* What the C++ standard library's headers call where they would throw an
 * exception or have found a broken precondition
 * (std::__libcpp_verbose_abort(const char *, ...)). */
void abort(void);
void _ZNSt3__122__libcpp_verbose_abortEPKcz(const char *format, ...)
{
    (void)format;
    abort();
}
