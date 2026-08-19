#ifndef BOOTLOADER_CONFIG_H
#define BOOTLOADER_CONFIG_H

#include <stdint.h>

/* ============================================================
 * 统一 BootLoader 配置：RA8T2 / MRAM / 双传输通道
 *   UDS 通道：CAN-FD + ISO-TP（高速，需 USB-CAN-FD 设备）
 *   IAP 通道：UART + LTM 协议（115200，USB-TTL 即可）
 * 两个通道共用同一套升级服务（uds_server）与运行区/备份区模型
 * ============================================================ */

/* BootLoader 版本 */
#define BL_VERSION_STRING           "1.1.0"
#define BL_META_VERSION             0x010100UL      /* 元数据版本：与 BL_VERSION_STRING 同步（1.1.0 → 0x010100），烧新 BootLoader 上电自动重置元数据 */

/* ============================================================
 * 传输通道开关：默认双通道全开，只出单通道版本时关掉另一个
 * ============================================================ */
#define BL_ENABLE_UDS               1       /* CAN-FD + ISO-TP */
#define BL_ENABLE_IAP               1       /* UART + LTM 协议 */

/* ============================================================
 * MRAM 分区：RA8T2 代码存储 0x02000000，512KB
 * 软件运行区 + 备份区（固定运行地址，App 只出一份 bin）：
 *
 *  0x02000000  Boot          32KB
 *  0x02008000  运行区        224KB   （App 固定链接 0x02008000）
 *  0x02040000  备份区        224KB   （升级前自动备份旧固件）
 *  0x02078000  元数据        32KB    （upgrade_req/pending/backup_valid/attempts）
 * ============================================================ */
#define BL_FLASH_BASE               0x02000000UL        /* MRAM 起始 */
#define BL_FLASH_SIZE               (512 * 1024)        /* 512KB */

#define BL_BOOT_START_ADDR          BL_FLASH_BASE       /* BootLoader 起始 */
#define BL_BOOT_SIZE                (32 * 1024)         /* BootLoader 32KB */

/* App 运行区 + 备份区（固定运行地址，App 只需出 bin） */
#define BL_APP_RUN_START            (BL_FLASH_BASE + BL_BOOT_SIZE)          /* 运行区 0x02008000 */
#define BL_APP_BACKUP_START         (BL_FLASH_BASE + 256 * 1024)            /* 备份区 0x02040000 */
#define BL_APP_BANK_SIZE            (224 * 1024)                            /* 每区 224KB */

/* 元数据区：启动标志（upgrade_req/pending/backup_valid/attempts），32B 编程线对齐 */
#define BL_META_ADDR                (BL_FLASH_BASE + 480 * 1024)            /* 0x02078000 */
#define BL_META_SIZE                (32 * 1024)
#define BL_META_MAGIC               0x5A5A5A5AUL

/* ============================================================
 * 跳转标记：RAM 顶部保留字（链接脚本 RAM_LENGTH 已减 4 预留）
 *   BootLoader 跳转前写入，App 启动据此识别"由 BootLoader 启动"，
 *   恢复被 __disable_irq 关闭的全局中断（软跳转不复位 PRIMASK）
 * ============================================================ */
#define BL_JUMP_FLAG_ADDR           (0x22000000UL + 0xEA000UL - 4)         /* RAM 顶部 0x220E9FFC */
#define BL_JUMP_MAGIC               0x4A554D50UL                            /* "JUMP" */

/* 回滚策略：新固件未确认（崩溃/看门狗复位）达到该次数则从备份区拷回旧固件 */
#define BL_MAX_ATTEMPTS             3

/* ============================================================
 * CAN-FD 配置（UDS 通道，与 LTM_HAL 一致）
 * ============================================================ */
#define BL_CANFD_ARB_BAUDRATE       1000000UL           /* 仲裁段 1Mbps */
#define BL_CANFD_DATA_BAUDRATE      2000000UL           /* 数据段 2Mbps */
#define BL_CANFD_UPGRADE_ID         0x7F0               /* 本机物理请求 ID（接收） */
#define BL_CANFD_RESPONSE_ID        0x7F1               /* 本机响应 ID（发送） */
#define BL_CANFD_BROADCAST_ID       0x7FF               /* 功能寻址广播 ID（接收） */

/* 接收白名单：上位机可能从多个 ID 发帧，全部列出 */
#define BL_CANFD_RX_ID_LIST         { BL_CANFD_UPGRADE_ID, BL_CANFD_BROADCAST_ID }

/* ============================================================
 * UART 配置（IAP 通道：SCI9，115200，LTM 协议承载 UDS PDU）
 * ============================================================ */
#define BL_UART_BAUDRATE            115200UL

/* ============================================================
 * 超时配置（ms）
 * ============================================================ */
#define BL_IDLE_TIMEOUT_MS          6000                /* IDLE 无指令 -> 跳转 App */
#define BL_RX_TIMEOUT_MS            1000                /* 下载会话接收超时 */
#define BL_MAX_RETRY                3                   /* 最大重试次数 */

/* ============================================================
 * MRAM 编程参数：RA8T2，32B 编程线
 * ============================================================ */
#define BL_MRAM_WRITE_UNIT          32                  /* 编程粒度 */
#define BL_MRAM_ERASE_BLOCK         32                  /* 擦除块大小 */

#endif /* BOOTLOADER_CONFIG_H */
