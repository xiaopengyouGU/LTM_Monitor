#ifndef PORT_UART_H
#define PORT_UART_H

#include <stdint.h>

/* ============================================================
 * UART 精简驱动（SCI9, 115200）—— IAP 通道
 * 接收：FIFO 批量模式 —— 自写 RXI 中断，一次中断倒空 FIFO，
 *       一次性回调协议层，不再逐字节中断（对比 LTM_HAL uart.c）
 * 发送：R_SCI_B_UART_Write + FSP TXI/TEI 中断
 * ============================================================ */

void port_uart_init(void);
void port_uart_close(void);                                        /* 关闭 UART（跳转 App 前释放外设） */
void port_uart_send(uint8_t *buf, uint16_t len);                /* 发送，非阻塞（上一帧未发完则丢弃） */
void port_uart_set_rxfeed(void (*feed)(uint8_t *buf, uint16_t len));  /* 设置批量接收回调（协议层） */

#endif /* PORT_UART_H */
