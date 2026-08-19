#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * LTM 通讯协议（与 LTM_HAL / LTM_Monitor 完全兼容，字节级一致）
 * 数据帧带校验位和长度，故帧尾没有必要存在
 * 帧结构：帧头(2) + 数据类型(1) + 数据长度(1) + 数据(N) + CRC校验位(2)
 * 固定帧长度：6 字节
 * ============================================================ */

#define RB_SIZE                 256     /* 环形缓冲区大小 */
#define PROTOCOL_DATA_SIZE      128     /* LTM 协议支持收发的最大数据（字节） */
#define SEND_BUF_SIZE           (128+6) /* 发送缓冲区大小（最大数据 + 固定帧长度） */
#define RECV_BUF_SIZE           SEND_BUF_SIZE   /* 接收数据缓冲区大小 */
#define FRAME_HEADER            0xA5B9  /* 小端存储（2字节）：B9 A5，极其冷门 */

#pragma pack(push, 1)
typedef struct {
    uint16_t header;    /* 帧头：0xA5B9 */
    uint8_t data_type;  /* 数据类型 */
    uint8_t data_len;   /* 数据长度：<= 128 字节 */
} protocol_header;
#pragma pack(pop)

/* 协议处理 API 接口 */
void protocol_init(uint8_t flag);
bool protocol_process(uint8_t *data_type, uint8_t *datas, uint16_t *data_len);  /* 协议解析 */
void protocol_package(uint8_t data_type, uint8_t *datas, uint16_t data_len);    /* 数据打包 */
uint8_t* protocol_datas(uint16_t *buf_len);                                     /* 获取打包后的数据（发送缓冲） */
void protocol_recv(uint8_t *buf, uint16_t buf_len);                             /* 直接写入环形缓冲区 */

#endif
