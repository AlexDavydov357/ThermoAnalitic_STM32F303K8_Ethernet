/**
 * lamps.h
 * Лампы нагрева LAMP1 (PA12), LAMP2 (PA11). Одна постоянная задача ведёт вспышку и отсчёт.
 */
#ifndef LAMPS_H
#define LAMPS_H

#include <stdbool.h>
#include <stdint.h>

void lamps_init(void);
void lamps_start_task(void);

/* Ответы (FLASH 1; COUNTD n; FLASH 0; FLASH_FORCEOFF; FLASH_BUSY_OR_TIME_ZERO;) шлёт задача ламп. */
bool lamps_request_on(void);
bool lamps_request_off(void);

void lamps_set_time(uint32_t seconds);
uint32_t lamps_get_time(void);

/* Для STATUS: горит ли и сколько секунд осталось. */
void lamps_get_state(bool *on, uint32_t *seconds_left);

/* Для тестов на ПК: req 1 = включить, 2 = выключить, 0 = нет запроса. */
void lamps_step(uint32_t now_ms, int req);

#endif /* LAMPS_H */
