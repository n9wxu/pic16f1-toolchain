/* Dependency injection through a constant table of function pointers: the
 * application (app.c) is written against this interface, the driver
 * (driver.c) supplies an implementation, and main.c puts the two together.
 * Compiled as a whole program, every call through the table is a direct
 * call; scripts/test-frames.sh checks that none is left indirect. */
#include <stdint.h>
struct io_ops { void (*put)(uint8_t c); uint8_t (*transform)(uint8_t c); };
extern const struct io_ops upper_ops;
void app_init(const struct io_ops *ops);
void app_run(const char *s);
