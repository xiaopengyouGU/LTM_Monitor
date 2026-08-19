#include "iso15765.h"
#include <string.h>

/* ============================================================
 * 内部上下文：收发状态收敛于此
 * ============================================================ */
typedef enum {
    TF_RX_IDLE = 0,         /* 空闲 */
    TF_RX_RECEIVING,        /* 多帧接收中 */
    TF_RX_COMPLETE,         /* 完整 PDU 就绪 */
    TF_RX_ERROR             /* 错误（需复位） */
} tf_rx_state_t;

typedef enum {
    TF_TX_IDLE = 0,         /* 空闲 */
    TF_TX_WAIT_FC,          /* 已发 FF，等待流控 */
    TF_TX_SENDING           /* 流控通过，按节奏发 CF */
} tf_tx_state_t;

typedef struct {
    /* 接收 */
    tf_rx_state_t rx_state;
    uint16_t rx_total;          /* 期望总长 */
    uint16_t rx_received;       /* 已收 */
    uint8_t  rx_expected_sn;    /* 期望 CF 序号 */
    uint8_t  rx_pdu[TF_MAX_PDU_SIZE];

    /* 发送 */
    tf_tx_state_t tx_state;
    uint16_t tx_id;             /* 本机发送 ID（回 FC 用） */
    uint16_t tx_pdu_id;         /* 本次发送 PDU 的目标 ID（FF/CF 用） */
    uint16_t tx_total;          /* PDU 总长 */
    uint16_t tx_sent;           /* 已发数据 */
    uint8_t  tx_sn;             /* CF 序号 */
    uint8_t  tx_bs;             /* 对端允许块大小 */
    uint8_t  tx_stmin;          /* 对端要求帧间隔 */
    uint32_t tx_next_ms;        /* 下一 CF 允许时间 */
    uint8_t  tx_pdu[TF_MAX_PDU_SIZE];

    const tf_ops_t *ops;
} tf_context_t;

static tf_context_t s_tf;       /* 唯一实例 */

/* FC 参数：BS=0 表示可连续发完；STmin=0 表示无间隔 */
#define TF_FC_BS        0
#define TF_FC_STMIN     0

/* 发送一帧 */
static void _send_frame(uint16_t id, const uint8_t *data, uint16_t len)
{
    if (s_tf.ops && s_tf.ops->send)
        s_tf.ops->send(id, data, len);
}

/* 回流控帧：FS=0 继续发送 */
static void _send_fc(uint8_t fs)
{
    uint8_t fc[3];
    fc[0] = (uint8_t)(0x30U | fs);
    fc[1] = TF_FC_BS;
    fc[2] = TF_FC_STMIN;
    _send_frame(s_tf.tx_id, fc, 3);
}

void tf_init(const tf_ops_t *ops, uint16_t tx_id)
{
    tf_context_t *ctx = &s_tf;
    memset(ctx, 0, sizeof(*ctx));
    ctx->ops   = ops;
    ctx->tx_id = tx_id;
}

bool tf_feed(const uint8_t *frame, uint16_t len)
{
    tf_context_t *ctx = &s_tf;
    if (!frame || len == 0) return false;

    uint8_t pci = frame[0];
    uint8_t type = pci & 0xF0U;

    /* ---- 流控帧 FC：推进发送状态机 ---- */
    if (type == 0x30U) {
        if (ctx->tx_state == TF_TX_WAIT_FC) {
            uint8_t fs = pci & 0x0FU;
            if (fs == 0x00U) {                  /* CTS：允许继续 */
                ctx->tx_bs    = (len > 1) ? frame[1] : 0;
                ctx->tx_stmin = (len > 2) ? frame[2] : 0;
                ctx->tx_state = TF_TX_SENDING;
                ctx->tx_next_ms = (ctx->ops && ctx->ops->get_ms) ? ctx->ops->get_ms() : 0;
            } else if (fs == 0x01U) {           /* WT：继续等待，重置定时 */
                ctx->tx_next_ms = (ctx->ops && ctx->ops->get_ms) ? ctx->ops->get_ms() : 0;
            } else {                            /* OVFLW：放弃发送 */
                ctx->tx_state = TF_TX_IDLE;
            }
        }
        return false;
    }

    /* ---- 单帧 SF：CAN-FD 用 SF_CanFD（pci=0x00，frame[1]=长度），兼容经典 SF（低 4 位） ---- */
    if (type == 0x00U) {
        uint16_t n;
        const uint8_t *payload;
        if (pci == 0x00U && len >= 2) {     /* SF_CanFD：长度在 frame[1] */
            n = frame[1];
            payload = &frame[2];
            if ((uint16_t)n + 2U > len) return false;
        } else {                            /* 经典 SF：低 4 位 */
            n = pci & 0x0FU;
            payload = &frame[1];
            if ((uint16_t)n + 1U > len) return false;
        }
        if (n > TF_MAX_PDU_SIZE) { ctx->rx_state = TF_RX_ERROR; return false; }
        ctx->rx_state = TF_RX_COMPLETE;
        ctx->rx_total = n;
        ctx->rx_received = n;
        memcpy(ctx->rx_pdu, payload, n);
        return true;
    }

    /* ---- 首帧 FF ---- */
    if (type == 0x10U) {
        uint16_t n = (uint16_t)(((uint16_t)(pci & 0x0FU) << 8) | frame[1]);
        if (n == 0 || n > TF_MAX_PDU_SIZE) { ctx->rx_state = TF_RX_ERROR; return false; }
        ctx->rx_state = TF_RX_RECEIVING;
        ctx->rx_total = n;
        ctx->rx_received = 0;
        ctx->rx_expected_sn = 1;
        uint16_t avail = (uint16_t)(len - 2U);
        if (avail > n) avail = n;
        memcpy(&ctx->rx_pdu[0], &frame[2], avail);
        ctx->rx_received = avail;
        _send_fc(0x00U);                        /* 回 FC：继续发送 */
        return false;
    }

    /* ---- 连续帧 CF ---- */
    if (type == 0x20U) {
        if (ctx->rx_state != TF_RX_RECEIVING) { ctx->rx_state = TF_RX_ERROR; return false; }
        uint8_t sn = pci & 0x0FU;
        if (sn != ctx->rx_expected_sn) { ctx->rx_state = TF_RX_ERROR; return false; }
        ctx->rx_expected_sn = (uint8_t)((ctx->rx_expected_sn + 1U) & 0x0FU);
        uint16_t remain = ctx->rx_total - ctx->rx_received;
        uint16_t n = (uint16_t)(len - 1U);
        if (n > remain) n = remain;
        memcpy(&ctx->rx_pdu[ctx->rx_received], &frame[1], n);
        ctx->rx_received += n;
        if (ctx->rx_received >= ctx->rx_total) {
            ctx->rx_state = TF_RX_COMPLETE;
            return true;
        }
        return false;
    }

    return false;
}

const uint8_t *tf_get_pdu(uint16_t *len)
{
    if (len) *len = s_tf.rx_total;
    return s_tf.rx_pdu;
}

bool tf_send_pdu(uint16_t id, const uint8_t *pdu, uint16_t len)
{
    tf_context_t *ctx = &s_tf;
    if (!pdu || len == 0 || len > TF_MAX_PDU_SIZE) return false;
    if (ctx->tx_state != TF_TX_IDLE) return false;      /* 发送忙 */

    /* 单帧：SF_CanFD 直发（pci=0x00 + 长度字节） */
    if (len <= TF_SF_MAX_LEN) {
        uint8_t frame[64];
        frame[0] = 0x00U;
        frame[1] = (uint8_t)len;
        memcpy(&frame[2], pdu, len);
        _send_frame(id, frame, (uint16_t)(len + 2));
        return true;
    }

    /* 多帧：保存 PDU，发 FF，等待 FC */
    memcpy(ctx->tx_pdu, pdu, len);
    ctx->tx_total  = len;
    ctx->tx_sent   = 0;
    ctx->tx_sn     = 0;
    ctx->tx_pdu_id = id;
    ctx->tx_state  = TF_TX_WAIT_FC;

    uint8_t frame[64];
    frame[0] = (uint8_t)(0x10U | ((len >> 8) & 0x0FU));
    frame[1] = (uint8_t)(len & 0xFFU);
    uint16_t first = (uint16_t)(len - 2U);
    if (first > TF_SF_MAX_LEN) first = TF_SF_MAX_LEN;
    memcpy(&frame[2], pdu, first);
    ctx->tx_sent = first;
    _send_frame(id, frame, (uint16_t)(first + 2));
    return true;
}

void tf_poll(void)
{
    tf_context_t *ctx = &s_tf;
    if (ctx->tx_state != TF_TX_SENDING) return;
    if (!ctx->ops || !ctx->ops->get_ms) return;

    uint32_t now = ctx->ops->get_ms();

    /* 帧间隔（STmin）未到 */
    if ((now - ctx->tx_next_ms) < ctx->tx_stmin)
        return;

    /* 发送一个 CF */
    uint16_t remain = ctx->tx_total - ctx->tx_sent;
    if (remain == 0) { ctx->tx_state = TF_TX_IDLE; return; }

    uint8_t frame[64];
    frame[0] = (uint8_t)(0x20U | (ctx->tx_sn & 0x0FU));
    uint16_t n = remain;
    if (n > TF_SF_MAX_LEN) n = TF_SF_MAX_LEN;
    memcpy(&frame[1], &ctx->tx_pdu[ctx->tx_sent], n);
    _send_frame(ctx->tx_pdu_id, frame, (uint16_t)(n + 1));

    ctx->tx_sent += n;
    ctx->tx_sn = (uint8_t)((ctx->tx_sn + 1U) & 0x0FU);
    ctx->tx_next_ms = now;

    /* 块边界：每 BS 帧后等待新的 FC */
    if (ctx->tx_bs > 0 && (ctx->tx_sent % (ctx->tx_bs * TF_SF_MAX_LEN)) == 0 &&
        ctx->tx_sent < ctx->tx_total) {
        ctx->tx_state = TF_TX_WAIT_FC;
    }
    if (ctx->tx_sent >= ctx->tx_total)
        ctx->tx_state = TF_TX_IDLE;
}