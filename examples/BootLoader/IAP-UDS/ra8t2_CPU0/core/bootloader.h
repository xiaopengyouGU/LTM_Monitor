#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * IAP BootLoader 公共 API
 * 应用层只依赖本头文件：bl_init + bl_process 完成全部升级逻辑
 * ============================================================ */

void bl_init(void);             /* 一键初始化：升级标志决策（备份/回滚/选择）-> IDLE/JUMP */
void bl_process(void);          /* 周期调用：LTM 协议解析 + IAP 服务 + 状态流转 */

/* App 侧接口 */
void bl_commit_app(void);       /* App 启动成功后调用：确认新固件有效 */
void bl_request_upgrade(void);  /* App 请求进入升级模式（置 upgrade_req，复位后进 IDLE） */
void bl_mark_pending(void);     /* 内部：0x37 下载完成后置 pending（BootLoader 使用） */
bool bl_app_is_valid(uint32_t app_addr);
void bl_jump_to_app(uint32_t app_addr);

#endif /* BOOTLOADER_H */
