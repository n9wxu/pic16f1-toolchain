/* Structs, a linked list, and dispatch through a table of function pointers. */
#include "bench.h"

struct node {
    uint8_t id;
    uint16_t value;
    struct node *next;
};

static struct node pool[8];

static uint16_t op_add(uint16_t a, uint16_t b) { return (uint16_t)(a + b); }
static uint16_t op_sub(uint16_t a, uint16_t b) { return (uint16_t)(a - b); }
static uint16_t op_xor(uint16_t a, uint16_t b) { return (uint16_t)(a ^ b); }
static uint16_t op_rot(uint16_t a, uint16_t b) { return (uint16_t)((a << 3) | (a >> 13)) ^ b; }

typedef uint16_t (*op_fn)(uint16_t, uint16_t);
static const op_fn ops[4] = { op_add, op_sub, op_xor, op_rot };

static struct node *build(void)
{
    struct node *head = 0;
    for (uint8_t i = 0; i < 8; i++) {
        struct node *n = &pool[i];
        n->id = i;
        n->value = (uint16_t)(1000u + (uint16_t)i * 321u);
        n->next = head;
        head = n;
    }
    return head;
}

static struct node *reverse(struct node *head)
{
    struct node *prev = 0;
    while (head) {
        struct node *next = head->next;
        head->next = prev;
        prev = head;
        head = next;
    }
    return prev;
}

void main(void)
{
    struct node *list;
    uint16_t acc = 0, order = 0;
    bench_init();
    list = reverse(build());
    for (struct node *n = list; n; n = n->next) {
        acc = ops[n->id & 3](acc, n->value);
        order = (uint16_t)(order * 3u + n->id);
    }
    bench_result16("acc", acc);
    bench_result16("order", order);
    /* ids 0..7 in order: sum of id * 3^(7-id) = 1636 */
    bench_exit(order == 0x0664 ? 0 : 1);
}
