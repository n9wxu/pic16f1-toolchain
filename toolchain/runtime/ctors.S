; Part of the startup code (see crt0.S): run the constructors of global
; objects before main. Linked only into a program that has some; the
; compiler refers to __do_global_ctors from every module that does.

#ifdef ONE_PAGE
#define PAGESEL(target)
#else
#define PAGESEL(target) movlp page(target)
#endif

    .section .init7,"ax",@progbits
    .globl  __do_global_ctors
__do_global_ctors:
    PAGESEL(__run_global_ctors)
    call    __run_global_ctors

; For the linker (see the end of crt0.S): this is part of _start, and calls
; the loop in runctors.c.
    .section .pic16f1.frames,"",@progbits
    .ascii  "F _start -\nC __run_global_ctors\n"
