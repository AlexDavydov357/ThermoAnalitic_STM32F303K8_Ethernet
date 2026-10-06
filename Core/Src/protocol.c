/**
 * protocol.c
 * Команды (регистр важен, конец команды ';'):
 *   LED n      n = 0|1           -> LED n;
 *   SHUT n     n = 0 открыть     -> SHUT_0 SHUT_OPENNING;   затем MOTOR_DONE_OPEN; | MOTOR_TIMEOUT;
 *              n = 1 закрыть     -> SHUT_1 SHUT_CLOSING;    затем MOTOR_DONE_CLOSE; | MOTOR_TIMEOUT;
 *              n = 2 стоп сразу  -> SHUT_2 SHUTTER_STOPPED;
 *              при неисправности концевиков -> SENSOR_FAULT;
 *   TIM n      n >= 0, секунды   -> TIM n;
 *   FLASH n    n = 1 включить, n = 0 выключить (ответы шлёт задача ламп)
 *   STATUS                       -> STATUS <заслонка> LAMP <0|1> <осталось_с>;
 *   VERSION                      -> VERSION <версия>;
 * Необязательный префикс адреса I<цифра> (слитно или через пробел) отбрасывается.
 * Пустая команда (';') молча игнорируется. Всё остальное -> UNKNOWN_COMMAND;
 */
#include "protocol.h"
#include "app.h"
#include "app_config.h"
#include "uart_io.h"
#include "shutter.h"
#include "lamps.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <string.h>

#define PROTOCOL_TASK_STACK_WORDS 192U
static StaticTask_t s_tcb;
static StackType_t s_stack[PROTOCOL_TASK_STACK_WORDS];

#define REPLY_WAIT pdMS_TO_TICKS(50)

void protocol_init(void)
{
}

static void reply(const char *s)
{
    uart_send(s, REPLY_WAIT);
}

static void reply_unknown(void)
{
    reply("UNKNOWN_COMMAND;");
}

/* Строго десятичное число без знака, до 9 цифр. */
static bool parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int n = 0;
    if (s == NULL || *s == '\0') {
        return false;
    }
    while (*s >= '0' && *s <= '9') {
        if (++n > 9) {
            return false;
        }
        v = v * 10U + (uint32_t)(*s - '0');
        s++;
    }
    if (*s != '\0') {
        return false;
    }
    *out = v;
    return true;
}

static void reply_word_num(const char *word, uint32_t n)
{
    msg_t m;
    msg_init(&m);
    msg_str(&m, word);
    msg_str(&m, " ");
    msg_u32(&m, n);
    msg_str(&m, ";");
    reply(m.b);
}

static void cmd_shut(uint32_t n)
{
    static const shutter_cmd_t map[] = { SHUTTER_CMD_OPEN, SHUTTER_CMD_CLOSE, SHUTTER_CMD_STOP };
    static const char *const ans[] = {
        "SHUT_0 SHUT_OPENNING;", "SHUT_1 SHUT_CLOSING;", "SHUT_2 SHUTTER_STOPPED;"
    };
    if (n > 2U) {
        reply_unknown();
        return;
    }
    if (n != 2U && shutter_sensor_fault()) {
        reply("SENSOR_FAULT;");
        return;
    }
    /* сначала ответ, потом команда: тогда MOTOR_DONE_* всегда придёт после него */
    reply(ans[n]);
    shutter_command(map[n]);
}

static void cmd_status(void)
{
    bool on;
    uint32_t left;
    msg_t m;
    lamps_get_state(&on, &left);
    msg_init(&m);
    msg_str(&m, "STATUS ");
    msg_str(&m, shutter_state_name(shutter_get_state()));
    msg_str(&m, on ? " LAMP 1 " : " LAMP 0 ");
    msg_u32(&m, left);
    msg_str(&m, ";");
    reply(m.b);
}

static void handle_line(char *line)
{
    char *p = line;

    /* префикс адреса I<цифра> */
    if (p[0] == 'I' && p[1] >= '0' && p[1] <= '9') {
        p += 2;
    }
    while (*p == ' ') {
        p++;
    }
    if (*p == '\0') {
        return;     /* пустая команда, например SYNC клиента после подключения */
    }

    char *word = p;
    char *arg = NULL;
    char *sp = strchr(p, ' ');
    if (sp != NULL) {
        *sp = '\0';
        arg = sp + 1;
        while (*arg == ' ') arg++;
        char *end = arg + strlen(arg);
        while (end > arg && end[-1] == ' ') *--end = '\0';
    }

    uint32_t n = 0;
    bool has_n = parse_u32(arg, &n);

    if (strcmp(word, "STATUS") == 0) {
        cmd_status();
    } else if (strcmp(word, "VERSION") == 0) {
        reply("VERSION " FW_VERSION_STR ";");
    } else if (!has_n) {
        reply_unknown();
    } else if (strcmp(word, "SHUT") == 0) {
        cmd_shut(n);
    } else if (strcmp(word, "FLASH") == 0) {
        if (n == 1U)      lamps_request_on();
        else if (n == 0U) lamps_request_off();
        else              reply_unknown();
    } else if (strcmp(word, "TIM") == 0) {
        lamps_set_time(n);
        reply_word_num("TIM", n);
    } else if (strcmp(word, "LED") == 0) {
        if (n > 1U) {
            reply_unknown();
        } else {
            shutter_set_led(n == 1U);
            reply_word_num("LED", n);
        }
    } else {
        reply_unknown();
    }
}

static char s_line[RX_LINE_MAX + 1U];
static uint32_t s_len;
static bool s_overflow;

/* Сборка строк до ';' из принятых байт. */
void protocol_feed(const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t c = buf[i];
        if (c == ';') {
            if (s_overflow) {
                reply_unknown();
            } else {
                s_line[s_len] = '\0';
                handle_line(s_line);
            }
            s_len = 0;
            s_overflow = false;
        } else if (c < 0x20U || c > 0x7EU) {
            /* \r, \n, нули и прочий мусор пропускаем */
        } else if (s_len < RX_LINE_MAX) {
            s_line[s_len++] = (char)c;
        } else {
            s_overflow = true;    /* слишком длинная: дочитываем до ';' и отвечаем UNKNOWN_COMMAND */
        }
    }
}

void protocol_send_boot(void)
{
    msg_t m;
    msg_init(&m);
    msg_str(&m, "BOOT ");
    msg_str(&m, app_reset_cause());
    msg_str(&m, " VERSION " FW_VERSION_STR ";");
    reply(m.b);
}

static void protocol_task(void *arg)
{
    uint8_t buf[16];
    (void)arg;

    protocol_send_boot();
    for (;;) {
        size_t got = uart_rx_read(buf, sizeof(buf), pdMS_TO_TICKS(100));
        app_heartbeat(HB_PROTOCOL);
        protocol_feed(buf, got);
    }
}

void protocol_start_task(void)
{
    xTaskCreateStatic(protocol_task, "protocol", PROTOCOL_TASK_STACK_WORDS, NULL, tskIDLE_PRIORITY + 2U,
                      s_stack, &s_tcb);
}
