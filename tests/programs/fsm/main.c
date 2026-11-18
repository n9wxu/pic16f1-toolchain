/* A switch-driven tokenizer and left-to-right calculator. */
#include "bench.h"

enum state { S_START, S_NUMBER, S_OP, S_ERROR };

static const char expr1[] = "12 + 34 * 5 - 6 / 4";
static const char expr2[] = "1000-1 *3+7";
static const char expr3[] = "5 + + 2";

static uint16_t apply(uint16_t acc, char op, uint16_t v)
{
    switch (op) {
    case '+': return (uint16_t)(acc + v);
    case '-': return (uint16_t)(acc - v);
    case '*': return (uint16_t)(acc * v);
    case '/': return v ? (uint16_t)(acc / v) : 0xFFFF;
    default:  return v;
    }
}

static uint8_t eval(const char *s, uint16_t *result)
{
    enum state st = S_START;
    uint16_t acc = 0, cur = 0;
    char op = 0;

    for (;; s++) {
        char c = *s;
        switch (st) {
        case S_START:
        case S_OP:
            if (c >= '0' && c <= '9') {
                cur = (uint16_t)(c - '0');
                st = S_NUMBER;
            } else if (c != ' ') {
                st = S_ERROR;
            }
            break;
        case S_NUMBER:
            if (c >= '0' && c <= '9') {
                cur = (uint16_t)(cur * 10u + (uint16_t)(c - '0'));
            } else {
                acc = apply(acc, op, cur);
                if (c == '+' || c == '-' || c == '*' || c == '/') {
                    op = c;
                    st = S_OP;
                } else if (c == ' ') {
                    /* wait for an operator */
                    op = 0;
                    cur = acc;
                    while (s[1] == ' ') s++;
                    if (s[1] == '+' || s[1] == '-' || s[1] == '*' || s[1] == '/') {
                        op = *++s;
                        st = S_OP;
                    } else if (s[1] != 0) {
                        st = S_ERROR;
                    }
                } else if (c != 0) {
                    st = S_ERROR;
                }
            }
            break;
        default:
            break;
        }
        if (st == S_ERROR) return 0;
        if (c == 0) break;
    }
    *result = acc;
    return st == S_NUMBER;
}

void main(void)
{
    uint16_t r1 = 0, r2 = 0, r3 = 0;
    uint8_t ok1, ok2, ok3;
    bench_init();
    ok1 = eval(expr1, &r1);
    ok2 = eval(expr2, &r2);
    ok3 = eval(expr3, &r3);
    bench_result16("r1", r1);
    bench_result16("r2", r2);
    bench_result16("ok", (uint16_t)((ok1 << 2) | (ok2 << 1) | ok3));
    /* ((12+34)*5-6)/4 = 56, (1000-1)*3+7 = 3004 */
    bench_exit(ok1 && ok2 && !ok3 && r1 == 56 && r2 == 3004 ? 0 : 1);
}
