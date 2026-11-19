/* The four memory functions of the runtime library are builtins even in a
 * build without the C library: a call with a small constant size is expanded
 * in place. A loop is never turned into a call. scripts/test-builtins.sh
 * checks both, and that -fno-builtin turns the first off. */
void *memcpy(void *, const void *, unsigned);
void *memset(void *, int, unsigned);
struct p { char a[4]; };
struct p x, y = {{1, 2, 3, 4}};
char buf[40], src[40];
volatile char sink;
__attribute__((noinline)) void copy4(void) { memcpy(&x, &y, 4); }
__attribute__((noinline)) void zero3(void) { memset(buf, 0, 3); }
__attribute__((noinline)) void fill(void) { for (unsigned char i = 0; i < 40; i++) buf[i] = 7; }
__attribute__((noinline)) void cp(void) { for (unsigned char i = 0; i < 40; i++) src[i] = buf[i]; }
void main(void)
{
    copy4(); fill(); cp(); zero3();
    sink = x.a[3] + src[39] + buf[0] + buf[3];   /* 4 + 7 + 0 + 7 */
}
