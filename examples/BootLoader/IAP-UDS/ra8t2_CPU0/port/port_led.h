#ifndef PORT_LED_H
#define PORT_LED_H

#include <stdint.h>

/* LED 平台抽象：BootLoader 自包含（直接调 FSP R_IOPORT） */

typedef enum {
    PORT_LED_STOP = 0,      /* 停机指示灯 */
    PORT_LED_RUN,           /* 运行指示灯 */
    PORT_LED_ON_OFF,        /* 上电指示灯 */
    PORT_LED_ERR,           /* 故障指示灯 */
    PORT_LED_COUNT
} port_led_id_t;

void port_led_init(void);                       /* 初始化 LED（引脚由 pin_data 配置） */
void port_led_set(port_led_id_t id, uint8_t state);   /* 0=灭 1=亮 */
void port_led_toggle(port_led_id_t id);         /* 翻转 */

#endif /* PORT_LED_H */