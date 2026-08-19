#include "port_canfd.h"
#include "port_uart.h"
#include "port_flash.h"
#include "port_sys.h"
#include "port_led.h"
#include "mock.h"

/* ============================================================
 * port 层 mock：双通道（CAN-FD / UART）+ 存储/时钟/LED
 * ============================================================ */

/* ---- CAN-FD ---- */
void port_canfd_init(void) {}
void port_canfd_set_filter(uint16_t id) { (void)id; }
void port_canfd_send(uint16_t id, const uint8_t *buf, uint16_t len) { mock_canfd_emit(id, buf, len); }
uint8_t port_canfd_recv(uint16_t *id, uint8_t *buf, uint16_t *len) { return mock_canfd_poll(id, buf, len); }
void port_canfd_set_rxcall(void (*cb)(void)) { (void)cb; }

/* ---- UART ---- */
static void (*s_feed)(uint8_t *buf, uint16_t len) = NULL;

void port_uart_init(void) {}
void port_uart_send(uint8_t *buf, uint16_t len) { mock_uart_emit(buf, len); }
void port_uart_set_rxfeed(void (*feed)(uint8_t *buf, uint16_t len)) { s_feed = feed; }

/* 测试桥接：把注入的 RX 字节通过 feed 一次性投递（模拟一次 RXI 批量回调） */
void mock_uart_deliver(void)
{
    uint8_t tmp[512];
    uint16_t n = mock_uart_rx_peek(tmp, sizeof(tmp));
    if (n && s_feed)
        s_feed(tmp, n);
}

/* ---- MRAM ---- */
int port_flash_init(void) { return 0; }
int port_flash_erase(uint32_t addr, uint32_t size) { mock_flash_erase(addr, size); return 0; }
int port_flash_write(uint32_t addr, const uint8_t *data, uint32_t len) { return mock_flash_write(addr, data, len); }
int port_flash_read(uint32_t addr, uint8_t *data, uint32_t len) { return mock_flash_read(addr, data, len); }
uint32_t port_flash_get_write_unit(void) { return 32; }

/* ---- 时钟 ---- */
void port_sys_init(void) {}
uint64_t port_sys_get_ms(void) { return mock_sys_now(); }
void port_sys_delay_ms(uint16_t ms) { mock_sys_tick(mock_sys_now() + ms); }
void port_sys_jump(uint32_t app_addr) { (void)app_addr; mock_sys_jump(); }

/* ---- LED ---- */
void port_led_init(void) {}
void port_led_set(port_led_id_t id, uint8_t state) { mock_led_set((uint8_t)id, state); }
void port_led_toggle(port_led_id_t id) { mock_led_toggle((uint8_t)id); }
