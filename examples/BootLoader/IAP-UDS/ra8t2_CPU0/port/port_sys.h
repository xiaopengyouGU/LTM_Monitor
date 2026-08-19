#ifndef PORT_SYS_H
#define PORT_SYS_H

#include <stdint.h>

/* 系统时钟抽象：BootLoader 自包含（SysTick 1ms + FSP 延时） */

void     port_sys_init(void);              /* 系统时钟 + IO 初始化 */
uint64_t port_sys_get_ms(void);            /* 系统运行毫秒 */
void     port_sys_delay_ms(uint16_t ms);   /* 毫秒延时 */
void     port_sys_jump(uint32_t app_addr); /* 跳转 App（关中断 + 向量表 + MSP） */

#endif /* PORT_SYS_H */