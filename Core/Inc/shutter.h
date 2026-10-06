/**
 * shutter.h
 * Управление мотором заслонки и концевиками. Всем железом мотора владеет одна задача
 * (defaultTask), остальные посылают ей команды через очередь.
 */
#ifndef SHUTTER_H
#define SHUTTER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SHUTTER_CMD_OPEN = 0,
    SHUTTER_CMD_CLOSE,
    SHUTTER_CMD_STOP
} shutter_cmd_t;

typedef enum {
    SHUTTER_ST_BETWEEN = 0,     /* стоит между концевиками */
    SHUTTER_ST_OPENED,
    SHUTTER_ST_CLOSED,
    SHUTTER_ST_OPENING,
    SHUTTER_ST_CLOSING,
    SHUTTER_ST_FAULT            /* оба концевика активны */
} shutter_state_t;

void shutter_init(void);
/* Тело задачи, вызывается из StartDefaultTask, не возвращается. */
void shutter_task_run(void);
/* Для тестов на ПК: начальная настройка и один цикл управления. */
void shutter_start(void);
void shutter_step(uint32_t now_ms);

bool shutter_command(shutter_cmd_t cmd);
shutter_state_t shutter_get_state(void);
const char *shutter_state_name(shutter_state_t st);
bool shutter_sensor_fault(void);

/* LED1: 1 = горит постоянно, 0 = показывает работу мотора */
void shutter_set_led(bool on);

#endif /* SHUTTER_H */
