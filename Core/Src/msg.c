/**
 * msg.c
 * Сборка коротких текстовых сообщений без printf.
 */
#include "uart_io.h"

void msg_init(msg_t *m)
{
    m->n = 0;
    m->b[0] = '\0';
}

void msg_str(msg_t *m, const char *s)
{
    while (*s && m->n < TX_MSG_LEN - 1U) {
        m->b[m->n++] = *s++;
    }
    m->b[m->n] = '\0';
}

void msg_u32(msg_t *m, uint32_t v)
{
    char t[11];
    int i = 0;
    do {
        t[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v && i < 10);
    while (i > 0 && m->n < TX_MSG_LEN - 1U) {
        m->b[m->n++] = t[--i];
    }
    m->b[m->n] = '\0';
}
