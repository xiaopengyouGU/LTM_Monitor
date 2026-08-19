#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * UDS（ISO 14229）服务：0x10/0x27/0x34/0x36/0x37
 * 内部状态（会话/安全/下载进度）收敛在实现内部，本头只暴露数据与接口
 * ============================================================ */

/* UDS 服务 ID */
#define UDS_SID_DIAGNOSTIC_SESSION    0x10U
#define UDS_SID_SECURITY_ACCESS       0x27U
#define UDS_SID_REQUEST_DOWNLOAD      0x34U
#define UDS_SID_TRANSFER_DATA         0x36U
#define UDS_SID_REQUEST_TRANSFER_EXIT 0x37U
#define UDS_SID_READ_DATA_BY_ID       0x22U
#define UDS_SID_ECU_RESET             0x11U

/* 子功能 */
#define UDS_SESSION_PROGRAMMING       0x02U

/* 负响应码 NRC */
#define UDS_NRC_POSITIVE              0x00U
#define UDS_NRC_SERVICE_NOT_SUPPORTED 0x11U
#define UDS_NRC_SUBFUNCTION_NOT_SUP   0x12U
#define UDS_NRC_INCORRECT_LENGTH      0x13U
#define UDS_NRC_CONDITIONS_NOT_CORRECT 0x22U
#define UDS_NRC_REQUEST_OUT_OF_RANGE  0x31U
#define UDS_NRC_SECURITY_ACCESS_DENIED 0x33U
#define UDS_NRC_GENERAL_PROGRAMMING_FAULT 0x72U

/* UDS 请求 */
typedef struct {
    uint8_t sid;
    uint8_t subfunc;
    uint8_t data[128];          /* 128 = LTM 协议载荷上限（UART 通道单块最大数据 126B） */
    uint16_t data_len;
} uds_request_t;

/* UDS 响应 */
typedef struct {
    uint8_t sid;                /* 正响应：SID+0x40；负响应：0x7F */
    uint8_t data[128];
    uint16_t data_len;
    uint8_t nrc;                /* 0x00 表示成功 */
} uds_response_t;

/* 固件存储回调：由平台层注入 */
typedef struct {
    int  (*erase)(uint32_t addr, uint32_t size);
    int  (*write)(uint32_t addr, const uint8_t *data, uint32_t len);
    int  (*read)(uint32_t addr, uint8_t *data, uint32_t len);
    void (*on_upgrade_done)(void);      /* 0x37 CRC 通过后调用（BootLoader 置 pending 标志） */
} uds_flash_ops_t;

void uds_init(const uds_flash_ops_t *ops, uint32_t app_base);   /* 初始化服务 */
void uds_set_target_addr(uint32_t addr);        /* AB 分区：指定本次下载目标地址 */
void uds_handle_request(const uds_request_t *req, uds_response_t *resp); /* 处理一帧 UDS 请求 */

/* 状态查询（供上层状态机联动，不暴露内部结构） */
bool uds_is_programming(void);      /* 是否处于编程会话 */
bool uds_is_downloading(void);      /* 下载传输是否激活 */
void uds_abort_download(void);      /* 中止下载，回默认会话 */

#endif /* UDS_SERVER_H */
