#ifndef PORT_FLASH_H
#define PORT_FLASH_H

#include <stdint.h>

/* MRAM 平台抽象：封装 FSP R_MRAM 驱动（RA8T2 代码存储） */

int  port_flash_init(void);                                  /* 打开 MRAM，0=成功 */
int  port_flash_erase(uint32_t addr, uint32_t size);         /* 按块擦除 */
int  port_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);  /* 编程（32B 对齐） */
int  port_flash_read(uint32_t addr, uint8_t *data, uint32_t len);         /* 读取 */
uint32_t port_flash_get_write_unit(void);                    /* 编程粒度 */

#endif /* PORT_FLASH_H */