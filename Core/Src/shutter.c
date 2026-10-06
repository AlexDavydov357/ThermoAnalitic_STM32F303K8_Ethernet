/**
 * shutter.c
 * Мотор заслонки: TIM1 CH1 (PA8, ШИМ), направление MOTOR_CW (PA7),
 * концевики i_1sw2OPENED (PB5) и i_0swCLOSED (PB6), активный уровень низкий.
 *
 * Скорость -100..+100: знак задаёт направление (+ открыть, CW = 0; - закрыть, CW = 1),
 * модуль равен скважности. Направление меняется только при скорости 0.
 */
#include "shutter.h"
#include "app.h"
#include "app_config.h"
#include "uart_io.h"
#include "main.h"
#include "tim.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

typedef enum { DIR_NONE = 0, DIR_OPEN, DIR_CLOSE } dir_t;

typedef struct {
    uint8_t stable;     /* отфильтрованное состояние, 1 = активен */
    uint8_t count;      /* сколько отсчётов подряд сырое значение отличается от stable */
} debounce_t;

static StaticQueue_t s_cmd_q;
static uint8_t s_cmd_storage[4U * sizeof(shutter_cmd_t)];
static QueueHandle_t s_cmdq;

static volatile shutter_state_t s_state = SHUTTER_ST_BETWEEN;
static volatile bool s_fault;
static volatile bool s_led_cmd;

static debounce_t s_opened, s_closed;
static dir_t s_dir;
static bool s_active;
static TickType_t s_start;
static int s_speed;
static int s_applied_speed = -1000;   /* заведомо другое значение, чтобы первый вызов записал выходы */

void shutter_init(void)
{
    s_cmdq = xQueueCreateStatic(4U, sizeof(shutter_cmd_t), s_cmd_storage, &s_cmd_q);
}

bool shutter_command(shutter_cmd_t cmd)
{
    return xQueueSend(s_cmdq, &cmd, pdMS_TO_TICKS(10)) == pdPASS;
}

shutter_state_t shutter_get_state(void)
{
    return s_state;
}

bool shutter_sensor_fault(void)
{
    return s_fault;
}

void shutter_set_led(bool on)
{
    s_led_cmd = on;
}

const char *shutter_state_name(shutter_state_t st)
{
    switch (st) {
    case SHUTTER_ST_OPENED:  return "OPENED";
    case SHUTTER_ST_CLOSED:  return "CLOSED";
    case SHUTTER_ST_OPENING: return "OPENING";
    case SHUTTER_ST_CLOSING: return "CLOSING";
    case SHUTTER_ST_FAULT:   return "FAULT";
    default:                 return "BETWEEN";
    }
}

static void debounce(debounce_t *d, uint8_t raw)
{
    if (raw == d->stable) {
        d->count = 0;
    } else if (++d->count >= SENSOR_DEBOUNCE_SAMPLES) {
        d->stable = raw;
        d->count = 0;
    }
}

/* Записывает скорость в ШИМ и пин направления, только если она изменилась. */
static void apply_speed(int speed)
{
    if (speed == s_applied_speed) {
        return;
    }
    s_applied_speed = speed;
    uint32_t duty = (uint32_t)(speed < 0 ? -speed : speed);
    HAL_GPIO_WritePin(MOTOR_CW_GPIO_Port, MOTOR_CW_Pin, speed < 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty);
}

static void stop_now(void)
{
    s_speed = 0;
    s_active = false;
    s_dir = DIR_NONE;
    apply_speed(0);
}

static void send(const char *s)
{
    uart_send(s, 0);    /* задача мотора никогда не ждёт очередь */
}

static void handle_command(shutter_cmd_t cmd, TickType_t now)
{
    switch (cmd) {
    case SHUTTER_CMD_OPEN:
    case SHUTTER_CMD_CLOSE:
        if (s_fault) {
            return;     /* протокол уже ответил SENSOR_FAULT */
        }
        s_dir = (cmd == SHUTTER_CMD_OPEN) ? DIR_OPEN : DIR_CLOSE;
        s_active = true;
        s_start = now;
        break;
    case SHUTTER_CMD_STOP:
    default:
        stop_now();
        break;
    }
}

/* Разгон к цели с учётом разворота: сначала до 0, потом в другую сторону. */
static void ramp(void)
{
    int target = (s_dir == DIR_OPEN) ? SHUTTER_SPEED_NOMINAL : -SHUTTER_SPEED_NOMINAL;
    if (s_speed < target) {
        s_speed += SHUTTER_ACCEL_PER_TICK;
        if (s_speed > target) s_speed = target;
    } else if (s_speed > target) {
        s_speed -= SHUTTER_ACCEL_PER_TICK;
        if (s_speed < target) s_speed = target;
    }
}

static shutter_state_t compute_state(void)
{
    if (s_fault)                       return SHUTTER_ST_FAULT;
    if (s_active && s_dir == DIR_OPEN)  return SHUTTER_ST_OPENING;
    if (s_active && s_dir == DIR_CLOSE) return SHUTTER_ST_CLOSING;
    if (s_opened.stable)               return SHUTTER_ST_OPENED;
    if (s_closed.stable)               return SHUTTER_ST_CLOSED;
    return SHUTTER_ST_BETWEEN;
}

/* Один цикл управления. Отдельно от задачи, чтобы его можно было проверить тестом на ПК. */
void shutter_step(TickType_t now)
{
    debounce(&s_opened, HAL_GPIO_ReadPin(i_1sw2OPENED_GPIO_Port, i_1sw2OPENED_Pin) == GPIO_PIN_RESET);
    debounce(&s_closed, HAL_GPIO_ReadPin(i_0swCLOSED_GPIO_Port, i_0swCLOSED_Pin) == GPIO_PIN_RESET);

    bool fault = s_opened.stable && s_closed.stable;
    if (fault && !s_fault) {
        stop_now();
        send("SENSOR_FAULT;");
    }
    s_fault = fault;

    shutter_cmd_t cmd;
    while (xQueueReceive(s_cmdq, &cmd, 0) == pdPASS) {
        handle_command(cmd, now);
    }

    if (s_active) {
        if (s_dir == DIR_OPEN && s_opened.stable) {
            stop_now();
            send("MOTOR_DONE_OPEN;");
        } else if (s_dir == DIR_CLOSE && s_closed.stable) {
            stop_now();
            send("MOTOR_DONE_CLOSE;");
        } else if ((TickType_t)(now - s_start) > pdMS_TO_TICKS(SHUTTER_TIMEOUT_MS)) {
            stop_now();
            send("MOTOR_TIMEOUT;");
        } else {
            ramp();
        }
    }

    apply_speed(s_speed);
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, (s_led_cmd || s_speed != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    s_state = compute_state();
}

void shutter_start(void)
{
    /* начальное состояние концевиков берём сразу, без ожидания фильтра */
    s_opened.stable = HAL_GPIO_ReadPin(i_1sw2OPENED_GPIO_Port, i_1sw2OPENED_Pin) == GPIO_PIN_RESET;
    s_closed.stable = HAL_GPIO_ReadPin(i_0swCLOSED_GPIO_Port, i_0swCLOSED_Pin) == GPIO_PIN_RESET;
    stop_now();
    s_state = compute_state();
}

void shutter_task_run(void)
{
    shutter_start();
    for (;;) {
        shutter_step(xTaskGetTickCount());
        app_heartbeat(HB_SHUTTER);
        app_watchdog_service();
        vTaskDelay(pdMS_TO_TICKS(SHUTTER_PERIOD_MS));
    }
}
