#include "iface.h"
static const struct io_ops *io;
void app_init(const struct io_ops *ops) { io = ops; }
void app_run(const char *s) { while (*s) io->put(io->transform((uint8_t)*s++)); }
