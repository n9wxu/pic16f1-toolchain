/* operator delete, on free of the C library. A class with a virtual
 * destructor refers to it whether or not the program ever deletes an
 * object, so it links without the C library too, and then does nothing. */
typedef unsigned int size_t;
void free(void *) __attribute__((weak));

void _ZdlPv(void *p)                                                /* delete */
{
    if (free)
        free(p);
}
void _ZdaPv(void *p) __attribute__((alias("_ZdlPv")));              /* delete[] */
void _ZdlPvj(void *p, size_t n) __attribute__((alias("_ZdlPv")));   /* sized */
void _ZdaPvj(void *p, size_t n) __attribute__((alias("_ZdlPv")));
