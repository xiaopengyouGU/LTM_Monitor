#include "mock.h"
#include <stdio.h>

int test_passes = 0;
int test_failures = 0;

/* ================= CAN-FD 总线 ================= */
#define MOCK_TX_MAX     256
#define MOCK_RX_MAX     64

typedef struct {
    uint16_t id;
    uint8_t  data[64];
    uint16_t len;
} mock_frame_t;

static mock_frame_t s_tx[MOCK_TX_MAX];
static uint16_t s_tx_cnt;

static mock_frame_t s_rx[MOCK_RX_MAX];
static uint16_t s_rx_cnt;
static uint16_t s_rx_pos;

void mock_canfd_tx_clear(void) { s_tx_cnt = 0; }
uint16_t mock_canfd_tx_count(void) { return s_tx_cnt; }
uint16_t mock_canfd_tx_id(uint16_t i) { return (i < s_tx_cnt) ? s_tx[i].id : 0; }
uint16_t mock_canfd_tx_len(uint16_t i) { return (i < s_tx_cnt) ? s_tx[i].len : 0; }
const uint8_t *mock_canfd_tx_data(uint16_t i) { return (i < s_tx_cnt) ? s_tx[i].data : NULL; }

void mock_canfd_inject(uint16_t id, const uint8_t *data, uint16_t len)
{
    if (s_rx_cnt >= MOCK_RX_MAX) return;
    s_rx[s_rx_cnt].id = id;
    s_rx[s_rx_cnt].len = len;
    if (len > 64) len = 64;
    memcpy(s_rx[s_rx_cnt].data, data, len);
    s_rx[s_rx_cnt].len = len;
    s_rx_cnt++;
}

uint8_t mock_canfd_poll(uint16_t *id, uint8_t *buf, uint16_t *len)
{
    if (s_rx_pos >= s_rx_cnt) return 0;
    *id  = s_rx[s_rx_pos].id;
    *len = s_rx[s_rx_pos].len;
    memcpy(buf, s_rx[s_rx_pos].data, s_rx[s_rx_pos].len);
    s_rx_pos++;
    if (s_rx_pos >= s_rx_cnt) { s_rx_cnt = 0; s_rx_pos = 0; }
    return 1;
}

void mock_canfd_emit(uint16_t id, const uint8_t *data, uint16_t len)
{
    if (s_tx_cnt >= MOCK_TX_MAX) return;
    s_tx[s_tx_cnt].id = id;
    s_tx[s_tx_cnt].len = len;
    if (len > 64) len = 64;
    memcpy(s_tx[s_tx_cnt].data, data, len);
    s_tx[s_tx_cnt].len = len;
    s_tx_cnt++;
}

/* ================= UART 字节流 ================= */
#define MOCK_UART_RX_MAX    512
#define MOCK_UART_TX_MAX    4096

static uint8_t s_uart_rx[MOCK_UART_RX_MAX];
static uint16_t s_uart_rx_cnt;
static uint8_t s_uart_tx[MOCK_UART_TX_MAX];
static uint16_t s_uart_tx_cnt;

void mock_uart_rx_clear(void) { s_uart_rx_cnt = 0; }

/* 注入一批字节并立即通过已注册 feed 投递（等价于一次 RXI 批量回调）
 * 若 feed 尚未注册（bl_init 前），字节进入队列，注册后由下一次注入/显式投递送出 */
void mock_uart_inject(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return;
    if (len > MOCK_UART_RX_MAX - s_uart_rx_cnt) len = MOCK_UART_RX_MAX - s_uart_rx_cnt;
    memcpy(&s_uart_rx[s_uart_rx_cnt], data, len);
    s_uart_rx_cnt += len;
}

uint16_t mock_uart_rx_pending(void) { return s_uart_rx_cnt; }

uint16_t mock_uart_rx_peek(uint8_t *out, uint16_t maxlen)
{
    uint16_t n = s_uart_rx_cnt;
    if (n > maxlen) n = maxlen;
    if (out && n) memcpy(out, s_uart_rx, n);
    s_uart_rx_cnt = 0;
    return n;
}

void mock_uart_tx_clear(void) { s_uart_tx_cnt = 0; }
uint16_t mock_uart_tx_len(void) { return s_uart_tx_cnt; }
void mock_uart_tx_dump(uint8_t *out, uint16_t maxlen)
{
    uint16_t n = s_uart_tx_cnt;
    if (n > maxlen) n = maxlen;
    memcpy(out, s_uart_tx, n);
}

void mock_uart_emit(uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return;
    if (len > MOCK_UART_TX_MAX - s_uart_tx_cnt) len = MOCK_UART_TX_MAX - s_uart_tx_cnt;
    memcpy(&s_uart_tx[s_uart_tx_cnt], data, len);
    s_uart_tx_cnt += len;
}

/* ================= MRAM 模拟 ================= */
#define MOCK_FLASH_BASE  0x02000000UL
#define MOCK_FLASH_SIZE  (512 * 1024)
#define MOCK_WRITE_UNIT  32

static uint8_t s_flash[MOCK_FLASH_SIZE];

static uint32_t _idx(uint32_t addr) { return addr - MOCK_FLASH_BASE; }

void mock_flash_reset(void) { memset(s_flash, 0xFF, sizeof(s_flash)); }

void mock_flash_erase(uint32_t addr, uint32_t size)
{
    if (addr < MOCK_FLASH_BASE || addr + size > MOCK_FLASH_BASE + MOCK_FLASH_SIZE) return;
    memset(&s_flash[_idx(addr)], 0xFF, size);
}

/* 模拟 MRAM 编程线：任意地址/长度，内部 32B 行处理，1->0 语义 */
int mock_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t off = 0;
    while (off < len)
    {
        uint32_t line_addr = (addr + off) & ~(MOCK_WRITE_UNIT - 1);   /* 本行起点 */
        uint32_t in_off    = (addr + off) - line_addr;                /* 行内偏移 */
        uint32_t chunk     = MOCK_WRITE_UNIT - in_off;
        if (chunk > len - off) chunk = len - off;

        uint8_t line[MOCK_WRITE_UNIT];
        memcpy(line, &s_flash[_idx(line_addr)], MOCK_WRITE_UNIT);
        for (uint32_t i = 0; i < chunk; i++)
            line[in_off + i] &= data[off + i];       /* MRAM 只能 1->0 */
        memcpy(&s_flash[_idx(line_addr)], line, MOCK_WRITE_UNIT);
        off += chunk;
    }
    return 0;
}

int mock_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (addr < MOCK_FLASH_BASE || addr + len > MOCK_FLASH_BASE + MOCK_FLASH_SIZE) return -1;
    memcpy(data, &s_flash[_idx(addr)], len);
    return 0;
}

void mock_flash_dump(uint32_t addr, uint32_t len, uint8_t *out)
{
    mock_flash_read(addr, out, len);
}

/* ================= 时钟 ================= */
static uint32_t s_ms;
static uint32_t s_jump_cnt;
void mock_sys_tick(uint32_t ms) { s_ms = ms; }
uint32_t mock_sys_now(void) { return s_ms; }
void mock_sys_jump_clear(void) { s_jump_cnt = 0; }
uint32_t mock_sys_jump_count(void) { return s_jump_cnt; }
void mock_sys_jump(void) { s_jump_cnt++; }

/* ================= LED ================= */
static uint8_t  s_led_state[4];
static uint32_t s_led_toggle_cnt[4];
void mock_led_set(uint8_t id, uint8_t state) { if (id < 4) s_led_state[id] = state; }
void mock_led_toggle(uint8_t id) { if (id < 4) { s_led_state[id] ^= 1; s_led_toggle_cnt[id]++; } }
uint8_t mock_led_state(uint8_t id) { return (id < 4) ? s_led_state[id] : 0; }
uint32_t mock_led_toggle_count(uint8_t id) { return (id < 4) ? s_led_toggle_cnt[id] : 0; }
