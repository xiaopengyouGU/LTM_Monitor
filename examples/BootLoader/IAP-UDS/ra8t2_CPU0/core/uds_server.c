#include "uds_server.h"
#include <string.h>

/* 大端 32 位读取：UDS 载荷（地址/长度/CRC）均为大端序 */
#define UDS_RD_BE32(p) (((uint32_t)(p)[0] << 24) | ((uint32_t)(p)[1] << 16) | \
                        ((uint32_t)(p)[2] << 8)  | ((uint32_t)(p)[3]))

/* ============================================================
 * CRC-32 (IEEE 802.3)：仅本模块使用
 * 多项式 0x04C11DB7（反射 0xEDB88320），初值/结果取反
 * ============================================================ */
/* CRC-32 内部状态累计：初值 0xFFFFFFFF（由调用者持有），返回未取反状态；
 * 最终校验值 = ~state。逐帧续算保证多帧固件与上位机一次性计算一致。 */
static uint32_t _crc32_update(const uint8_t *data, uint32_t len, uint32_t state)
{
    for (uint32_t i = 0; i < len; i++) {
        state ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (state & 1U)
                state = (state >> 1) ^ 0xEDB88320U;
            else
                state >>= 1;
        }
    }
    return state;                       /* 内部状态（未取反） */
}


/* ============================================================
 * 内部上下文：应用层不可见，UDS 会话/下载状态全部收敛于此
 * ============================================================ */
typedef enum {
    UDS_SESSION_DEFAULT = 0x01,
    UDS_SESSION_PROGRAM = 0x02,
} uds_session_t;

typedef struct {
    uds_session_t session;              /* 当前会话 */
    bool security_unlocked;             /* 安全访问是否通过 */
    bool download_active;               /* 下载传输中 */
    uint32_t app_base;                  /* 默认 App 起始（兼容单分区） */
    uint32_t target_addr;               /* 本次下载目标地址（AB 分区） */
    uint32_t app_size;                  /* 本次固件大小 */
    uint32_t write_addr;                /* 当前写入地址 */
    uint8_t  block_seq;                 /* 块序号计数器 */
    uint32_t crc_calc;                  /* 边写边算的 CRC */
    const uds_flash_ops_t *flash_ops;   /* 存储回调 */
} uds_context_t;

static uds_context_t s_uds;             /* 唯一实例 */


void uds_init(const uds_flash_ops_t *ops, uint32_t app_base)
{
    uds_context_t *ctx = &s_uds;
    ctx->session          = UDS_SESSION_DEFAULT;
    ctx->security_unlocked = false;
    ctx->download_active  = false;
    ctx->app_base         = app_base;
    ctx->target_addr      = app_base;      /* 默认与 app_base 一致 */
    ctx->app_size         = 0;
    ctx->write_addr       = 0;
    ctx->block_seq        = 0;
    ctx->crc_calc         = 0xFFFFFFFFUL;
    ctx->flash_ops        = ops;
}

/* 负响应构造 */
static void _nrc(uds_response_t *resp, uint8_t sid, uint8_t nrc)
{
    resp->sid      = 0x7FU;
    resp->data[0]  = sid;
    resp->data[1]  = nrc;
    resp->data_len = 2;
    resp->nrc      = nrc;
}

/* 正响应构造 */
static void _pos(uds_response_t *resp, uint8_t sid)
{
    resp->sid      = (uint8_t)(sid + 0x40U);
    resp->data_len = 0;
    resp->nrc      = UDS_NRC_POSITIVE;
}

static void _add_byte(uds_response_t *resp, uint8_t b)
{
    if (resp->data_len < sizeof(resp->data))
        resp->data[resp->data_len++] = b;
}

static void _add_word32(uds_response_t *resp, uint32_t v)
{
    _add_byte(resp, (uint8_t)(v >> 24));
    _add_byte(resp, (uint8_t)(v >> 16));
    _add_byte(resp, (uint8_t)(v >> 8));
    _add_byte(resp, (uint8_t)(v));
}

/* 0x10 诊断会话控制 */
static void _session_control(uds_context_t *ctx, const uds_request_t *req, uds_response_t *resp)
{
    if (req->data_len != 0) { _nrc(resp, req->sid, UDS_NRC_INCORRECT_LENGTH); return; }
    if (req->subfunc == UDS_SESSION_PROGRAMMING) {
        ctx->session = UDS_SESSION_PROGRAM;
    } else if (req->subfunc == UDS_SESSION_DEFAULT) {
        ctx->session = UDS_SESSION_DEFAULT;
        ctx->download_active = false;           /* 退出编程：丢弃未完成传输 */
        ctx->security_unlocked = false;
    } else {
        _nrc(resp, req->sid, UDS_NRC_SUBFUNCTION_NOT_SUP);
        return;
    }
    _pos(resp, req->sid);
    _add_byte(resp, (uint8_t)req->subfunc);
}

/* 0x27 安全访问：种子固定 0x00（产线简化），密钥固定 0x5A */
static void _security_access(uds_context_t *ctx, const uds_request_t *req, uds_response_t *resp)
{
    if (req->subfunc == 0x01U) {                /* 请求种子 */
        if (req->data_len != 0) { _nrc(resp, req->sid, UDS_NRC_INCORRECT_LENGTH); return; }
        _pos(resp, req->sid);
        _add_byte(resp, 0x01U);
        _add_byte(resp, 0x00U);                 /* 种子：0x0000 */
        _add_byte(resp, 0x00U);
    } else if (req->subfunc == 0x02U) {         /* 发送密钥 */
        if (req->data_len != 2 || req->data[0] != 0x00U || req->data[1] != 0x5AU) {
            _nrc(resp, req->sid, UDS_NRC_SECURITY_ACCESS_DENIED);
            return;
        }
        ctx->security_unlocked = true;
        _pos(resp, req->sid);
        _add_byte(resp, 0x02U);
    } else {
        _nrc(resp, req->sid, UDS_NRC_SUBFUNCTION_NOT_SUP);
    }
}

/* 0x34 请求下载：地址(4) + 长度(4) */
static void _request_download(uds_context_t *ctx, const uds_request_t *req, uds_response_t *resp)
{
    if (ctx->session != UDS_SESSION_PROGRAM) { _nrc(resp, req->sid, UDS_NRC_CONDITIONS_NOT_CORRECT); return; }
    if (!ctx->security_unlocked)             { _nrc(resp, req->sid, UDS_NRC_SECURITY_ACCESS_DENIED); return; }
    /* 0x34 请求：DFI(1) + ALFI(1) + 地址(4) + 长度(4) */
    if (req->data_len != 10)                 { _nrc(resp, req->sid, UDS_NRC_INCORRECT_LENGTH); return; }
    if (req->data[0] != 0x00 || req->data[1] != 0x00) {   /* 仅支持 4B 地址 + 4B 长度格式 */
        _nrc(resp, req->sid, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    uint32_t addr = UDS_RD_BE32(&req->data[2]);
    uint32_t size = UDS_RD_BE32(&req->data[6]);
    if (addr != ctx->target_addr || size == 0 || size > (224 * 1024)) {
        _nrc(resp, req->sid, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    if (ctx->flash_ops && ctx->flash_ops->erase) {
        if (ctx->flash_ops->erase(addr, size) != 0) {
            _nrc(resp, req->sid, UDS_NRC_GENERAL_PROGRAMMING_FAULT);
            return;
        }
    }
    ctx->download_active = true;
    ctx->app_size        = size;
    ctx->write_addr      = addr;
    ctx->block_seq       = 0;
    ctx->crc_calc        = 0xFFFFFFFFUL;

    _pos(resp, req->sid);
    _add_word32(resp, size);                /* 返回可接收长度（单块） */
}

/* 0x36 传输数据：块序号(1) + 数据 */
static void _transfer_data(uds_context_t *ctx, const uds_request_t *req, uds_response_t *resp)
{
    if (!ctx->download_active) { _nrc(resp, req->sid, UDS_NRC_CONDITIONS_NOT_CORRECT); return; }
    if (req->data_len < 1)     { _nrc(resp, req->sid, UDS_NRC_INCORRECT_LENGTH); return; }

    uint8_t seq = req->data[0];
    ctx->block_seq++;
    if (seq != ctx->block_seq) { _nrc(resp, req->sid, UDS_NRC_GENERAL_PROGRAMMING_FAULT); return; }

    const uint8_t *payload = &req->data[1];
    uint16_t payload_len = (uint16_t)(req->data_len - 1);
    uint32_t remaining = ctx->app_size - (ctx->write_addr - ctx->app_base);
    if ((uint32_t)payload_len > remaining) { _nrc(resp, req->sid, UDS_NRC_REQUEST_OUT_OF_RANGE); return; }

    if (ctx->flash_ops && ctx->flash_ops->write) {
        if (ctx->flash_ops->write(ctx->write_addr, payload, payload_len) != 0) {
            _nrc(resp, req->sid, UDS_NRC_GENERAL_PROGRAMMING_FAULT);
            return;
        }
    }
    ctx->crc_calc = _crc32_update(payload, payload_len, ctx->crc_calc);
    ctx->write_addr += payload_len;

    _pos(resp, req->sid);
    _add_byte(resp, seq);
}

/* 0x37 请求退出传输：回读校验 CRC */
static void _transfer_exit(uds_context_t *ctx, const uds_request_t *req, uds_response_t *resp)
{
    if (req->data_len != 0) { _nrc(resp, req->sid, UDS_NRC_INCORRECT_LENGTH); return; }
    if (!ctx->download_active) { _nrc(resp, req->sid, UDS_NRC_CONDITIONS_NOT_CORRECT); return; }
    ctx->download_active = false;

    /* 回读已写区域计算 CRC，与边写边算比对（AB 分区：读取实际写入的 target_addr） */
    uint32_t crc_read = 0xFFFFFFFFUL;
    if (ctx->flash_ops && ctx->flash_ops->read) {
        uint8_t buf[32];
        uint32_t remain = ctx->app_size;
        uint32_t addr   = ctx->target_addr;
        while (remain > 0) {
            uint32_t n = (remain > sizeof(buf)) ? (uint32_t)sizeof(buf) : remain;
            if (ctx->flash_ops->read(addr, buf, n) != 0) {
                _nrc(resp, req->sid, UDS_NRC_GENERAL_PROGRAMMING_FAULT);
                return;
            }
            crc_read = _crc32_update(buf, n, crc_read);
            addr   += n;
            remain -= n;
        }
    }

    if (crc_read != ctx->crc_calc) {
        _nrc(resp, req->sid, UDS_NRC_GENERAL_PROGRAMMING_FAULT);
        return;
    }

    /* 升级完成：通知平台层（AB 分区置 pending 标志，下次复位切换） */
    if (ctx->flash_ops && ctx->flash_ops->on_upgrade_done)
        ctx->flash_ops->on_upgrade_done();

    _pos(resp, req->sid);
    _add_word32(resp, ~crc_read);       /* 返回标准 CRC-32（取反） */
}

/* 0x22 读取数据：F190=版本信息 */
static void _read_data(uds_context_t *ctx, const uds_request_t *req, uds_response_t *resp)
{
    (void)ctx;
    if (req->data_len != 2) { _nrc(resp, req->sid, UDS_NRC_INCORRECT_LENGTH); return; }
    uint16_t did = (uint16_t)(((uint16_t)req->data[0] << 8) | req->data[1]);
    if (did == 0xF190U) {
        _pos(resp, req->sid);
        _add_byte(resp, 0xF1U);
        _add_byte(resp, 0x90U);
        _add_byte(resp, 0x01U);             /* 版本 1.0 */
        _add_byte(resp, 0x00U);
    } else {
        _nrc(resp, req->sid, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }
}

/* 0x11 ECU 复位：软复位 */
static void _ecu_reset(uds_context_t *ctx, const uds_request_t *req, uds_response_t *resp)
{
    (void)ctx;
    if (req->subfunc != 0x01U) { _nrc(resp, req->sid, UDS_NRC_SUBFUNCTION_NOT_SUP); return; }
    _pos(resp, req->sid);
    _add_byte(resp, 0x01U);
}

void uds_handle_request(const uds_request_t *req, uds_response_t *resp)
{
    uds_context_t *ctx = &s_uds;
    if (!req || !resp) return;
    memset(resp, 0, sizeof(*resp));
    resp->nrc = UDS_NRC_POSITIVE;

    switch (req->sid)
    {
        case UDS_SID_DIAGNOSTIC_SESSION:     _session_control(ctx, req, resp);     break;
        case UDS_SID_SECURITY_ACCESS:        _security_access(ctx, req, resp);     break;
        case UDS_SID_REQUEST_DOWNLOAD:       _request_download(ctx, req, resp);    break;
        case UDS_SID_TRANSFER_DATA:          _transfer_data(ctx, req, resp);       break;
        case UDS_SID_REQUEST_TRANSFER_EXIT:  _transfer_exit(ctx, req, resp);       break;
        case UDS_SID_READ_DATA_BY_ID:        _read_data(ctx, req, resp);           break;
        case UDS_SID_ECU_RESET:              _ecu_reset(ctx, req, resp);           break;
        default:                             _nrc(resp, req->sid, UDS_NRC_SERVICE_NOT_SUPPORTED); break;
    }
}

/* AB 分区：指定本次下载目标地址（由 BootLoader 在进入编程会话时设置） */
void uds_set_target_addr(uint32_t addr)
{
    s_uds.target_addr = addr;
}

/* ============================================================
 * 状态查询：供上层状态机联动，不暴露内部结构
 * ============================================================ */
bool uds_is_programming(void)
{
    return (s_uds.session == UDS_SESSION_PROGRAM);
}

bool uds_is_downloading(void)
{
    return s_uds.download_active;
}

void uds_abort_download(void)      /* 终止下载 */
{
    s_uds.session           = UDS_SESSION_DEFAULT;
    s_uds.download_active   = false;
    s_uds.security_unlocked = false;
}
