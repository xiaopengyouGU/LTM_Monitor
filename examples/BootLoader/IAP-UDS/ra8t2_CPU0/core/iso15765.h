#ifndef ISO15765_H
#define ISO15765_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * ISO 15765-2（ISO-TP）传输层：SF/FF/CF/FC 完整收发
 * 接收：重组多帧为完整 PDU，收到 FF 自动回 FC
 * 发送：单帧直发，多帧 FF -> 等 FC -> 按 BS/STmin 发 CF
 * 底层发送与时基通过 ops 注入（与硬件解耦）
 * ============================================================ */

#define TF_MAX_PDU_SIZE     512         /* 重组/发送缓冲（UDS 足够） */
#define TF_SF_MAX_LEN       62          /* CAN-FD 单帧最大数据长度 */

typedef struct {
    void (*send)(uint16_t id, const uint8_t *data, uint16_t len);   /* 发送一帧 */
    uint32_t (*get_ms)(void);                                       /* 毫秒时基 */
} tf_ops_t;

void tf_init(const tf_ops_t *ops, uint16_t tx_id);                  /* 初始化（tx_id=本机发送 ID） */
bool tf_feed(const uint8_t *frame, uint16_t len);                   /* 喂一帧；返回 true=完整 PDU 就绪 */
const uint8_t *tf_get_pdu(uint16_t *len);                           /* 获取完整 PDU（只读） */
bool tf_send_pdu(uint16_t id, const uint8_t *pdu, uint16_t len);    /* 发送 PDU：自动单帧/多帧 */
void tf_poll(void);                                                 /* 周期调用：推进多帧发送 */

#endif /* ISO15765_H */