#ifndef MOCK_H
#define MOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ============================================================
 * 平台无关测试 mock：模拟 CAN-FD 总线 / UART 字节流 / MRAM / 系统时钟
 * 供 core 层在主机上直接编译验证协议逻辑（双通道）
 * ============================================================ */

/* ---- CAN-FD 总线模拟：TX 记录 + RX 注入 ---- */
void mock_canfd_tx_clear(void);
uint16_t mock_canfd_tx_count(void);
uint16_t mock_canfd_tx_id(uint16_t i);
uint16_t mock_canfd_tx_len(uint16_t i);
const uint8_t *mock_canfd_tx_data(uint16_t i);
void mock_canfd_inject(uint16_t id, const uint8_t *data, uint16_t len);
uint8_t mock_canfd_poll(uint16_t *id, uint8_t *buf, uint16_t *len);
void mock_canfd_emit(uint16_t id, const uint8_t *data, uint16_t len);

/* ---- UART 字节流模拟：RX 注入（模拟一次 RXI 批量回调）+ TX 记录 ---- */
void mock_uart_rx_clear(void);
void mock_uart_inject(const uint8_t *data, uint16_t len);   /* 注入一批字节，等价于一次 RXI 批量回调 */
uint16_t mock_uart_rx_pending(void);                        /* 待投递字节数 */
uint16_t mock_uart_rx_peek(uint8_t *out, uint16_t maxlen);  /* 读出队列并清空（供投递） */
void mock_uart_deliver(void);                               /* 通过已注册 feed 一次性投递（模拟 RXI 中断） */
void mock_uart_tx_clear(void);
uint16_t mock_uart_tx_len(void);
void mock_uart_tx_dump(uint8_t *out, uint16_t maxlen);      /* 拼接全部发送字节 */
void mock_uart_emit(uint8_t *data, uint16_t len);           /* port 层发送 → 记录 */

/* ---- MRAM 模拟：RAM 数组 + 1->0 编程语义 ---- */
void mock_flash_reset(void);                    /* 全部擦除：0xFF */
void mock_flash_erase(uint32_t addr, uint32_t size);
int  mock_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);   /* 模拟编程线 */
int  mock_flash_read(uint32_t addr, uint8_t *data, uint32_t len);
void mock_flash_dump(uint32_t addr, uint32_t len, uint8_t *out);

/* ---- 系统时钟模拟：手动 tick ---- */
void mock_sys_tick(uint32_t ms);                /* 推进时钟 */
uint32_t mock_sys_now(void);
void mock_sys_jump_clear(void);                 /* 清跳转计数 */
void mock_sys_jump(void);                       /* 记录一次 App 跳转 */
uint32_t mock_sys_jump_count(void);             /* App 跳转次数（验证窗口超时跳转） */

/* ---- LED 模拟 ---- */
void mock_led_set(uint8_t id, uint8_t state);
void mock_led_toggle(uint8_t id);
uint8_t mock_led_state(uint8_t id);
uint32_t mock_led_toggle_count(uint8_t id);

/* 断言工具 */
#define TEST_ASSERT(cond) do { \
    if (!(cond)) { printf("[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); test_failures++; } \
    else { test_passes++; } \
} while(0)

extern int test_passes;
extern int test_failures;

#endif /* MOCK_H */
