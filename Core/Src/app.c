/**
 * app.c
 * Безопасное состояние выходов, сторожевой таймер IWDG, хуки FreeRTOS, создание задач.
 */
#include "app.h"
#include "app_config.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "uart_io.h"
#include "shutter.h"
#include "lamps.h"
#include "protocol.h"

static volatile TickType_t s_heartbeat[HB_COUNT];
static const char *s_reset_cause = "OTHER";

#define PA8_PIN_POS     8U

/* PA8 как обычный выход в «1». Драйвер мотора инверсный: высокий уровень на PA8 = мотор стоит
 * (так было и в старой прошивке: PWM2, CCR=0 давал 100 % высокого уровня). */
static void pa8_force_stop_level(void)
{
    GPIOA->BSRR = GPIO_PIN_8;
    GPIOA->MODER = (GPIOA->MODER & ~(3U << (PA8_PIN_POS * 2U))) | (1U << (PA8_PIN_POS * 2U));
}

void app_outputs_safe(void)
{
    pa8_force_stop_level();
    GPIOA->BSRR = ((uint32_t)(LAMP1_Pin | LAMP2_Pin | MOTOR_CW_Pin)) << 16U;
}

void app_early_init(void)
{
    uint32_t csr = RCC->CSR;
    if (csr & RCC_CSR_IWDGRSTF)      s_reset_cause = "IWDG";
    else if (csr & RCC_CSR_SFTRSTF)  s_reset_cause = "SOFT";
    else if (csr & (RCC_CSR_PORRSTF | RCC_CSR_V18PWRRSTF)) s_reset_cause = "POWER";
    else if (csr & RCC_CSR_PINRSTF)  s_reset_cause = "PIN";
    RCC->CSR |= RCC_CSR_RMVF;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    app_outputs_safe();
}

const char *app_reset_cause(void)
{
    return s_reset_cause;
}

/* IWDG от LSI ~40 кГц, делитель /64 -> 625 Гц */
static void watchdog_start(void)
{
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;   /* при остановке в отладчике IWDG тоже стоит */
    IWDG->KR = 0xCCCCU;
    IWDG->KR = 0x5555U;
    IWDG->PR = 4U;                                    /* /64 */
    IWDG->RLR = (WDG_TIMEOUT_MS * 625U) / 1000U;
    while (IWDG->SR != 0U) {
    }
    IWDG->KR = 0xAAAAU;
}

void app_init(void)
{
    GPIO_InitTypeDef gi = {0};

    /* Концевики замыкают вход на землю: включаем внутреннюю подтяжку вверх,
     * чтобы при обрыве провода вход не «плавал». */
    gi.Pin = i_3_Pin | i_0swCLOSED_Pin | i_1sw2OPENED_Pin;
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gi);

    uart_io_init();
    shutter_init();
    lamps_init();
    protocol_init();
    watchdog_start();
}

void app_create_tasks(void)
{
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < HB_COUNT; i++) {
        s_heartbeat[i] = now;
    }
    uart_io_start_task();
    lamps_start_task();
    protocol_start_task();
    /* задача заслонки = defaultTask CubeMX (StartDefaultTask) */
}

void app_heartbeat(heartbeat_id_t id)
{
    s_heartbeat[id] = xTaskGetTickCount();
}

void app_watchdog_service(void)
{
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < HB_COUNT; i++) {
        if ((TickType_t)(now - s_heartbeat[i]) > pdMS_TO_TICKS(HEARTBEAT_MAX_AGE_MS)) {
            return;     /* какая-то задача зависла: не сбрасываем IWDG, будет перезагрузка */
        }
    }
    IWDG->KR = 0xAAAAU;
}

void app_fatal(void)
{
    __disable_irq();
    app_outputs_safe();
    NVIC_SystemReset();
    for (;;) {
    }
}

/* ---- хуки FreeRTOS ---- */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    app_fatal();
}

void vApplicationMallocFailedHook(void)
{
    app_fatal();
}
