/**
 * uart_io.h
 * Обмен по USART1 (к модулю USR-TCP232-T2).
 * Приём: прерывание складывает байты в кольцевой буфер, задача протокола их читает.
 * Передача: очередь готовых сообщений, отдельная задача отправляет их по одному через DMA.
 */
#ifndef UART_IO_H
#define UART_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "app_config.h"

void uart_io_init(void);
void uart_io_start_task(void);

/* Вызывается из USART1_IRQHandler до HAL_UART_IRQHandler. */
void uart_io_usart1_irq(void);
/* Вызывается из USART1_IRQHandler после HAL_UART_IRQHandler. */
void uart_io_usart1_irq_post(void);

/* Читает принятые байты, ждёт не дольше wait. Возвращает число байт. */
size_t uart_rx_read(uint8_t *buf, size_t max, TickType_t wait);

/* Ставит сообщение в очередь на отправку (копирует строку).
 * wait = 0 для задачи мотора, чтобы она никогда не блокировалась. */
bool uart_send(const char *s, TickType_t wait);

/* Простой сборщик сообщений без printf. */
typedef struct {
    char b[TX_MSG_LEN];
    uint8_t n;
} msg_t;

void msg_init(msg_t *m);
void msg_str(msg_t *m, const char *s);
void msg_u32(msg_t *m, uint32_t v);

/* Счётчики для отладки */
extern volatile uint32_t uart_tx_dropped;
extern volatile uint32_t uart_rx_overflow;
extern volatile uint32_t uart_rx_errors;

#endif /* UART_IO_H */
