#include "bench.h"
#include "iface.h"
void main(void) { bench_init(); app_init(&upper_ops); app_run("injected\n"); bench_exit(0); }
