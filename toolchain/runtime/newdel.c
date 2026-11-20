/* operator new, on malloc of the C library (pic16f1-cc --libc). Written in
 * C under its C++ names; size_t is unsigned int here. A program that never
 * allocates links none of this. It returns a null pointer when memory runs
 * out: there are no exceptions to throw. */
typedef unsigned int size_t;
void *malloc(size_t);

void *_Znwj(size_t n) { return malloc(n ? n : 1); }                 /* new */
void *_Znaj(size_t n) __attribute__((alias("_Znwj")));              /* new[] */
