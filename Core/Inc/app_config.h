/**
 * app_config.h
 * Настройки прошивки управления лампами нагрева и заслонкой (FZK_THERMO_LAMPSHUTTER).
 * Все времена в миллисекундах, если не указано иное.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define FW_VERSION_STR              "2.0"

/* ---- Заслонка ---- */
#define SHUTTER_PERIOD_MS           2U      /* период цикла управления мотором */
#define SHUTTER_SPEED_NOMINAL       100     /* скорость 0..100 = скважность ШИМ, % */
#define SHUTTER_ACCEL_PER_TICK      1       /* разгон: +1 % за цикл, до 100 % за 200 мс */
#define SHUTTER_TIMEOUT_MS          5000U   /* движение дольше этого = MOTOR_TIMEOUT */
#define SENSOR_DEBOUNCE_SAMPLES     5U      /* концевик считается сработавшим после N одинаковых отсчётов (5 x 2 мс = 10 мс) */

/* ---- UART ---- */
#define RX_STREAM_SIZE              128U    /* кольцевой буфер приёма из прерывания, байт */
#define RX_LINE_MAX                 32U     /* максимальная длина команды до ';' */
#define TX_MSG_LEN                  48U     /* максимальная длина одного ответа, включая ';' */
#define TX_QUEUE_DEPTH              8U      /* сколько ответов может ждать отправки */
#define TX_DMA_TIMEOUT_MS           50U     /* если DMA не закончил за это время, передача сбрасывается */

/* ---- Сторожевой таймер ---- */
#define WDG_TIMEOUT_MS              1000U   /* IWDG: перезагрузка, если задачи зависли */
#define HEARTBEAT_MAX_AGE_MS        800U    /* задача считается живой, если отметилась не раньше чем столько назад */

#endif /* APP_CONFIG_H */
