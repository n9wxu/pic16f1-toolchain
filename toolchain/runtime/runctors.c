/* Run the constructors of global objects: the functions listed in
 * .init_array, which the linker script puts in flash between these two
 * names, lowest priority number first. Called from the startup code
 * (ctors.S), after data is copied and .bss cleared and before main.
 *
 * Nothing runs destructors: a program here does not end, and exit stops
 * the processor. */
typedef void (*constructor)(void);
extern const constructor __init_array_start[], __init_array_end[];

void __run_global_ctors(void)
{
    const constructor *p;
    for (p = __init_array_start; p != __init_array_end; ++p)
        (*p)();
}
