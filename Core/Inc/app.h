/**
 * app.h
 * Общие функции приложения: безопасное состояние выходов, сторожевой таймер,
 * отметки «задача жива», создание задач.
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>

typedef enum {
    HB_SHUTTER = 0,
    HB_PROTOCOL,
    HB_LAMPS,
    HB_UART_TX,
    HB_COUNT
} heartbeat_id_t;

/* Вызывается первой строкой после HAL_Init(): PA8 в «стоп», лампы и CW выключены. */
void app_early_init(void);
/* Вызывается после инициализации периферии CubeMX, до запуска планировщика. */
void app_init(void);
/* Создаёт задачи приложения (вызывается из MX_FREERTOS_Init). */
void app_create_tasks(void);

/* Немедленно переводит выходы в безопасное состояние. Можно вызывать из любого контекста, в т.ч. из HardFault. */
void app_outputs_safe(void);
/* Фатальная ошибка: безопасные выходы и перезагрузка. Не возвращается. */
void app_fatal(void) __attribute__((noreturn));

void app_heartbeat(heartbeat_id_t id);
/* Сбрасывает IWDG, только если все задачи недавно отметились. Вызывается из цикла заслонки. */
void app_watchdog_service(void);

/* Причина последнего сброса для сообщения BOOT: "POWER", "PIN", "IWDG", "SOFT", "OTHER". */
const char *app_reset_cause(void);

#endif /* APP_H */
