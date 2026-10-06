/**
 * protocol.h
 * Разбор команд из UART: строки до ';', ответы через uart_send.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

void protocol_init(void);
void protocol_start_task(void);

/* Для тестов на ПК */
#include <stddef.h>
#include <stdint.h>
void protocol_feed(const uint8_t *buf, size_t n);
void protocol_send_boot(void);

#endif /* PROTOCOL_H */
