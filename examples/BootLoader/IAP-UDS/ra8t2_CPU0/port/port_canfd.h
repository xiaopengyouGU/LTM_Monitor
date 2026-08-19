#ifndef PORT_CANFD_H
#define PORT_CANFD_H

#include <stdint.h>

/* CAN-FD 平台抽象：封装 LTM_HAL 的 CAN-FD 驱动 */

void     port_canfd_init(void);                              /* 初始化 CAN-FD */
void     port_canfd_close(void);                             /* 关闭 CAN-FD（跳转 App 前释放外设） */
void     port_canfd_set_filter(uint16_t id);                 /* 白名单过滤 */
void     port_canfd_send(uint16_t id, const uint8_t *buf, uint16_t len);   /* 发送 CAN-FD 帧 */
uint8_t  port_canfd_recv(uint16_t *id, uint8_t *buf, uint16_t *len);       /* 接收：1=有帧 0=无 */
void     port_canfd_set_rxcall(void (*cb)(void));            /* 接收中断回调 */

#endif /* PORT_CANFD_H */
