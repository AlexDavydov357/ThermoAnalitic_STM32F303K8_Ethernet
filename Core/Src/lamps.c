/**
 * lamps.c
 * Вспышка ламп на заданное время с отсчётом раз в секунду.
 *
 * FLASH 1 при времени T: лампы включаются, сразу FLASH 1; и COUNTD T;
 * затем COUNTD T-1 ... COUNTD 1 раз в секунду, через T секунд лампы гаснут и FLASH 0;.
 * Отсчёт идёт от момента включения, без накопления ошибки.
 */
#include "lamps.h"
#include "app.h"
#include "uart_io.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

typedef enum { LAMP_REQ_ON = 1, LAMP_REQ_OFF = 2 } lamp_req_t;

#define LAMPS_TASK_STACK_WORDS 128U
static StaticTask_t s_tcb;
static StackType_t s_stack[LAMPS_TASK_STACK_WORDS];

static StaticQueue_t s_q;
static uint8_t s_q_storage[4U * sizeof(lamp_req_t)];
static QueueHandle_t s_req;

static volatile uint32_t s_time_s;      /* время вспышки из команды TIM */
static volatile bool s_on;
static volatile uint32_t s_left;        /* последнее отправленное значение COUNTD */

void lamps_init(void)
{
    s_req = xQueueCreateStatic(4U, sizeof(lamp_req_t), s_q_storage, &s_q);
}

void lamps_set_time(uint32_t seconds)
{
    s_time_s = seconds;
}

uint32_t lamps_get_time(void)
{
    return s_time_s;
}

void lamps_get_state(bool *on, uint32_t *seconds_left)
{
    taskENTER_CRITICAL();
    *on = s_on;
    *seconds_left = s_on ? s_left : 0U;
    taskEXIT_CRITICAL();
}

static bool post(lamp_req_t r)
{
    return xQueueSend(s_req, &r, pdMS_TO_TICKS(10)) == pdPASS;
}

bool lamps_request_on(void)  { return post(LAMP_REQ_ON); }
bool lamps_request_off(void) { return post(LAMP_REQ_OFF); }

static void lamps_write(bool on)
{
    GPIO_PinState st = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(LAMP1_GPIO_Port, LAMP1_Pin, st);
    HAL_GPIO_WritePin(LAMP2_GPIO_Port, LAMP2_Pin, st);
}

static void send_countd(uint32_t n)
{
    msg_t m;
    msg_init(&m);
    msg_str(&m, "COUNTD ");
    msg_u32(&m, n);
    msg_str(&m, ";");
    uart_send(m.b, pdMS_TO_TICKS(20));
}

static TickType_t s_start;
static uint32_t s_elapsed_s;    /* сколько полных секунд прошло с включения */
static uint32_t s_total_s;

/* Сколько можно ждать следующего события (для таймаута очереди). */
TickType_t lamps_wait_ticks(TickType_t now)
{
    TickType_t wait = pdMS_TO_TICKS(200);   /* не реже, чтобы отмечаться для IWDG */
    if (s_on) {
        TickType_t next = s_start + (TickType_t)(s_elapsed_s + 1U) * pdMS_TO_TICKS(1000);
        TickType_t to_next = (TickType_t)(next - now);
        if ((int32_t)to_next < 0) to_next = 0;
        if (to_next < wait) wait = to_next;
    }
    return wait;
}

/* Обработка запроса (req = 0, если его нет) и хода времени. */
void lamps_step(TickType_t now, int req)
{
    if (req == LAMP_REQ_ON) {
        if (s_on || s_time_s == 0U) {
            uart_send("FLASH_BUSY_OR_TIME_ZERO;", pdMS_TO_TICKS(20));
        } else {
            s_total_s = s_time_s;
            s_elapsed_s = 0;
            s_start = now;
            lamps_write(true);
            s_left = s_total_s;
            s_on = true;
            uart_send("FLASH 1;", pdMS_TO_TICKS(20));
            send_countd(s_total_s);
        }
    } else if (req == LAMP_REQ_OFF) {
        if (s_on) {
            lamps_write(false);
            s_on = false;
            uart_send("FLASH_FORCEOFF;", pdMS_TO_TICKS(20));
        } else {
            uart_send("FLASH 0;", pdMS_TO_TICKS(20));
        }
    }

    while (s_on && (TickType_t)(now - s_start) >= (TickType_t)(s_elapsed_s + 1U) * pdMS_TO_TICKS(1000)) {
        s_elapsed_s++;
        if (s_elapsed_s >= s_total_s) {
            lamps_write(false);
            s_on = false;
            uart_send("FLASH 0;", pdMS_TO_TICKS(20));
        } else {
            s_left = s_total_s - s_elapsed_s;
            send_countd(s_left);
        }
    }
}

static void lamps_task(void *arg)
{
    (void)arg;
    lamps_write(false);
    for (;;) {
        lamp_req_t r;
        int req = 0;
        if (xQueueReceive(s_req, &r, lamps_wait_ticks(xTaskGetTickCount())) == pdPASS) {
            req = (int)r;
        }
        lamps_step(xTaskGetTickCount(), req);
        app_heartbeat(HB_LAMPS);
    }
}

void lamps_start_task(void)
{
    xTaskCreateStatic(lamps_task, "lamps", LAMPS_TASK_STACK_WORDS, NULL, tskIDLE_PRIORITY + 3U,
                      s_stack, &s_tcb);
}
