/**
 * uart_io.c
 * Приём и передача по USART1 без общих буферов между задачами.
 */
#include "uart_io.h"
#include "app.h"
#include "usart.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"
#include <string.h>

volatile uint32_t uart_tx_dropped;
volatile uint32_t uart_rx_overflow;
volatile uint32_t uart_rx_errors;

/* ---- приём ---- */
static StaticStreamBuffer_t s_rx_sb;
static uint8_t s_rx_storage[RX_STREAM_SIZE + 1U];
static StreamBufferHandle_t s_rx;

/* ---- передача ---- */
static StaticQueue_t s_tx_q;
static uint8_t s_tx_q_storage[TX_QUEUE_DEPTH * TX_MSG_LEN];
static QueueHandle_t s_txq;

static StaticSemaphore_t s_tx_done_sem;
static SemaphoreHandle_t s_tx_done;

#define TX_TASK_STACK_WORDS 128U
static StaticTask_t s_tx_tcb;
static StackType_t s_tx_stack[TX_TASK_STACK_WORDS];

void uart_io_init(void)
{
    s_rx = xStreamBufferCreateStatic(RX_STREAM_SIZE, 1U, s_rx_storage, &s_rx_sb);
    s_txq = xQueueCreateStatic(TX_QUEUE_DEPTH, TX_MSG_LEN, s_tx_q_storage, &s_tx_q);
    s_tx_done = xSemaphoreCreateBinaryStatic(&s_tx_done_sem);

    /* Приём ведём сами по RXNE: без HAL_UART_Receive_IT и его перезапусков.
     * Сбрасываем возможные старые ошибки и мусор в RDR. */
    __HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    (void)huart1.Instance->RDR;
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
}

void uart_io_usart1_irq(void)
{
    USART_TypeDef *u = huart1.Instance;
    uint32_t isr = u->ISR;
    BaseType_t woken = pdFALSE;

    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
        u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_PECF;
        uart_rx_errors++;
    }
    if (isr & USART_ISR_RXNE) {
        uint8_t c = (uint8_t)u->RDR;
        if (xStreamBufferSendFromISR(s_rx, &c, 1U, &woken) != 1U) {
            uart_rx_overflow++;
        }
    }
    portYIELD_FROM_ISR(woken);
}

void uart_io_usart1_irq_post(void)
{
    /* HAL при ошибке приёма может снять RXNEIE: возвращаем, приём не должен останавливаться */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
}

size_t uart_rx_read(uint8_t *buf, size_t max, TickType_t wait)
{
    return xStreamBufferReceive(s_rx, buf, max, wait);
}

bool uart_send(const char *s, TickType_t wait)
{
    char m[TX_MSG_LEN];
    size_t n = strlen(s);
    if (n >= TX_MSG_LEN) {
        n = TX_MSG_LEN - 1U;
    }
    memcpy(m, s, n);
    m[n] = '\0';
    if (xQueueSend(s_txq, m, wait) != pdPASS) {
        uart_tx_dropped++;
        return false;
    }
    return true;
}

static void tx_task(void *arg)
{
    static char cur[TX_MSG_LEN];    /* DMA читает только этот буфер */
    char next[TX_MSG_LEN];          /* сюда принимаем, пока DMA ещё отправляет cur */
    (void)arg;
    xSemaphoreGive(s_tx_done);

    for (;;) {
        app_heartbeat(HB_UART_TX);
        if (xQueueReceive(s_txq, next, pdMS_TO_TICKS(100)) != pdPASS) {
            continue;
        }
        /* ждём окончания предыдущей передачи; если DMA завис, сбрасываем его.
         * Только после этого cur свободен для нового сообщения. */
        if (xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(TX_DMA_TIMEOUT_MS)) != pdTRUE) {
            HAL_UART_AbortTransmit(&huart1);
        }
        memcpy(cur, next, TX_MSG_LEN);
        if (HAL_UART_Transmit_DMA(&huart1, (uint8_t *)cur, (uint16_t)strlen(cur)) != HAL_OK) {
            xSemaphoreGive(s_tx_done);
            uart_tx_dropped++;
        }
    }
}

void uart_io_start_task(void)
{
    xTaskCreateStatic(tx_task, "uart_tx", TX_TASK_STACK_WORDS, NULL, tskIDLE_PRIORITY + 2U,
                      s_tx_stack, &s_tx_tcb);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t woken = pdFALSE;
    if (huart->Instance == USART1) {
        xSemaphoreGiveFromISR(s_tx_done, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    BaseType_t woken = pdFALSE;
    if (huart->Instance == USART1 && (huart->ErrorCode & HAL_UART_ERROR_DMA)) {
        /* ошибка DMA передачи: разрешаем следующую */
        xSemaphoreGiveFromISR(s_tx_done, &woken);
    }
    portYIELD_FROM_ISR(woken);
}
