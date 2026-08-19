#include "bootloader.h"
#include "bootloader_config.h"
#include "uds_server.h"
#include "iso15765.h"
#include "protocol/ltm_commut.h"
#include "port_canfd.h"
#include "port_uart.h"
#include "port_flash.h"
#include "port_sys.h"
#include "port_led.h"

#include <string.h>

/* ============================================================
 * 统一 BootLoader：UDS(CAN-FD+ISO-TP) + IAP(UART+LTM) 双通道
 * 升级服务只有一份（uds_server），两个传输通道只是"壳"：
 *   - CAN-FD 通道：ISO-TP 重组/分段，载荷即 UDS PDU
 *   - UART 通道：LTM 帧承载，Data_User_Defined 载荷即 UDS PDU
 * 任一通道收到命令都刷新同一个活动时间戳，共享会话/下载状态
 * ============================================================ */

/* ============================================================
 * 内部上下文：应用层不可见，全部状态收敛于此
 * ============================================================ */
typedef enum {
    BL_STATE_INIT = 0,      /* 启动：检查升级标志与 App 有效性 */
    BL_STATE_IDLE,          /* 等待升级命令 */
    BL_STATE_PROGRAM,       /* 编程会话（下载中） */
    BL_STATE_JUMP,          /* 跳转 App */
    BL_STATE_ERROR,         /* 错误 */
} bl_state_t;

typedef struct {
    bl_state_t state;           /* 当前状态 */
    uint32_t app_start_addr;    /* App 起始地址 */
    uint32_t app_max_size;      /* App 最大尺寸 */
    uint32_t last_ms;           /* 上次活动时间 */
    uint8_t  error_code;        /* 错误码 */

    /* 收帧缓冲（CAN-FD 通道） */
    uint16_t rx_id;             /* 当前帧 CAN ID（用于多 ID 分发） */
    uint8_t rx_buf[64];
    uint16_t rx_len;
} bl_context_t;

static bl_context_t s_bl;                       /* 唯一实例 */

static uint32_t s_led_last_ms;                  /* LED 心跳时间戳（显示层，独立于协议超时） */

/* 存储回调：UDS 擦写用 port_flash 提供 */
static int _flash_erase(uint32_t addr, uint32_t size) { return port_flash_erase(addr, size); }
static int _flash_write(uint32_t addr, const uint8_t *data, uint32_t len) { return port_flash_write(addr, data, len); }
static int _flash_read(uint32_t addr, uint8_t *data, uint32_t len) { return port_flash_read(addr, data, len); }

static const uds_flash_ops_t s_flash_ops = {
    .erase = _flash_erase,
    .write = _flash_write,
    .read  = _flash_read,
    .on_upgrade_done = bl_mark_pending,     /* 0x37 完成后置 pending，下次复位运行新固件 */
};

/* ISO-TP 底层回调：发送走 CAN-FD，时基走系统 */
#if BL_ENABLE_UDS
static void _canfd_send(uint16_t id, const uint8_t *data, uint16_t len) { port_canfd_send(id, data, len); }
static uint32_t _sys_ms(void) { return (uint32_t)port_sys_get_ms(); }

static const tf_ops_t s_tf_ops = {
    .send   = _canfd_send,
    .get_ms = _sys_ms,
};
#endif

/* ============================================================
 * 周期节流器：距上次 >= timeout_ms 则刷新时间戳并返回 true
 * 无符号减法自动处理回绕；timeout_ms = 0 时等价于"记录活动时间"
 * ============================================================ */
static bool _bl_timeout_elapsed(uint32_t *last_ms, uint32_t timeout_ms)
{
    uint32_t now = (uint32_t)port_sys_get_ms();
    if ((now - *last_ms) >= timeout_ms) {
        *last_ms = now;
        return true;
    }
    return false;
}

/* ============================================================
 * 运行区 + 备份区元数据（MRAM 元数据区，32B 编程线对齐）
 *   upgrade_req : 1=App 请求进入升级模式（复位后备份运行区并进 IDLE）
 *   pending     : 1=新固件已下载完成，等待 App 确认
 *   backup_valid: 1=备份区保存着上一版可用固件
 *   attempts    : 新固件启动失败连续计数（超限则从备份区恢复）
 * ============================================================ */
typedef struct {
    uint32_t magic;         /* BL_META_MAGIC */
    uint32_t boot_version;  /* BL_META_VERSION：烧新 BootLoader 后版本不匹配 → 重置元数据 */
    uint8_t  upgrade_req;   /* 升级请求标志 */
    uint8_t  pending;       /* 新固件待确认 */
    uint8_t  backup_valid;  /* 备份区是否保存有效旧固件 */
    uint8_t  attempts;      /* 启动失败计数 */
} bl_meta_t;

static uint8_t s_meta_buf[BL_MRAM_WRITE_UNIT];  /* 元数据写缓冲，32B 对齐 */

static void _meta_load(bl_meta_t *meta)
{
    if (!meta) return;
    memset(meta, 0, sizeof(*meta));
    port_flash_read(BL_META_ADDR, s_meta_buf, sizeof(s_meta_buf));
    memcpy(meta, s_meta_buf, sizeof(*meta));
}

static void _meta_store(const bl_meta_t *meta)
{
    if (!meta) return;
    memset(s_meta_buf, 0xFF, sizeof(s_meta_buf));
    memcpy(s_meta_buf, meta, sizeof(*meta));
    port_flash_erase(BL_META_ADDR, BL_MRAM_WRITE_UNIT);   /* 先擦后写 */
    port_flash_write(BL_META_ADDR, s_meta_buf, sizeof(s_meta_buf));
}

/* 整区拷贝：src -> dst，长度 size（按 32B 编程线搬运） */
static int _copy_region(uint32_t src, uint32_t dst, uint32_t size)
{
    uint8_t buf[BL_MRAM_WRITE_UNIT];
    if (port_flash_erase(dst, size) != 0) return -1;   /* 先擦目标区 */
    for (uint32_t off = 0; off < size; off += BL_MRAM_WRITE_UNIT) {
        if (port_flash_read(src + off, buf, sizeof(buf)) != 0) return -1;
        if (port_flash_write(dst + off, buf, sizeof(buf)) != 0) return -1;
    }
    return 0;
}

/* 升级前：把运行区当前固件备份到备份区 */
static int _backup_run(void)
{
    return _copy_region(BL_APP_RUN_START, BL_APP_BACKUP_START, BL_APP_BANK_SIZE);
}

/* 回滚：从备份区恢复运行区 */
static int _restore_run(void)
{
    return _copy_region(BL_APP_BACKUP_START, BL_APP_RUN_START, BL_APP_BANK_SIZE);
}

/* App 请求进入升级模式：置 upgrade_req，复位后 BootLoader 备份并进 IDLE */
void bl_request_upgrade(void)
{
    bl_meta_t meta;
    _meta_load(&meta);
    if (meta.magic != BL_META_MAGIC) {
        memset(&meta, 0, sizeof(meta));
        meta.magic = BL_META_MAGIC;
    }
    meta.upgrade_req = 1;
    meta.attempts    = 0;
    _meta_store(&meta);
}

/* 0x37 下载完成：新固件已写入运行区，置 pending 等待 App 确认 */
void bl_mark_pending(void)
{
    bl_meta_t meta;
    _meta_load(&meta);
    if (meta.magic != BL_META_MAGIC) return;
    meta.pending  = 1;      /* 备份区已在上电时备份完成（backup_valid） */
    meta.attempts = 0;
    _meta_store(&meta);
}

/* App 启动成功后调用：确认新固件有效，清 pending */
void bl_commit_app(void)
{
    bl_meta_t meta;
    _meta_load(&meta);
    if (meta.magic != BL_META_MAGIC) return;

    meta.pending     = 0;      /* 新固件确认可用 */
    meta.upgrade_req = 0;
    meta.attempts    = 0;
    _meta_store(&meta);
}

/* App 有效：栈指针在 RAM 区、复位向量非 0 且落在 MRAM
 * 通过 port_flash_read 读取，保持平台无关（不直接解引用硬件地址） */
bool bl_app_is_valid(uint32_t app_addr)
{
    uint32_t sp = 0, pc = 0;
    if (port_flash_read(app_addr,      (uint8_t *)&sp, sizeof(sp)) != 0) return false;
    if (port_flash_read(app_addr + 4U, (uint8_t *)&pc, sizeof(pc)) != 0) return false;
    if (sp < 0x20000000UL || sp > 0x23000000UL) return false;
    if (pc < BL_FLASH_BASE || pc >= (BL_FLASH_BASE + BL_FLASH_SIZE)) return false;
    return true;
}

/* ============================================================
 * 跳转 App：设置向量表 + 栈指针，复位到 App 入口
 * ============================================================ */
void bl_jump_to_app(uint32_t app_addr)
{
    port_sys_jump(app_addr);    /* 平台层完成关中断/向量表/栈指针切换 */
}

/* ============================================================
 * 初始化：运行区 + 备份区决策
 *   - upgrade_req：备份当前运行区到备份区，清标志，进 IDLE 等升级
 *   - pending：新固件待确认；启动失败计数超限则从备份区恢复运行区
 *   - 运行区有效：仍进 IDLE（每次复位都有固定升级窗口，超时后才跳转 App）
 *   - 运行区无效且备份可用：恢复后同样进 IDLE 等待
 * ============================================================ */
void bl_init(void)
{
    bl_context_t *ctx = &s_bl;
    memset(ctx, 0, sizeof(*ctx));
    ctx->app_start_addr = BL_APP_RUN_START;   /* 运行区固定 */
    ctx->app_max_size   = BL_APP_BANK_SIZE;

    /* 传输通道初始化 + 升级服务（只有一份） */
#if BL_ENABLE_UDS
    tf_init(&s_tf_ops, BL_CANFD_RESPONSE_ID);           /* ISO-TP：发送走 CAN-FD */
#endif
#if BL_ENABLE_IAP
    ltm_commut_init();                                  /* LTM 协议：发送走 UART */
    ltm_commut_set_send(port_uart_send);
    port_uart_set_rxfeed(ltm_commut_recv);              /* RXI 批量接收 → 一次性喂协议层 */
    ltm_commut_printf("[BL] boot v" BL_VERSION_STRING "\r\n");   /* 上电版本标识：确认烧录版本/串口链路 */
#endif
    uds_init(&s_flash_ops, BL_APP_RUN_START);

    port_led_set(PORT_LED_ON_OFF, 1);       /* 上电指示灯常亮 */
    s_led_last_ms = (uint32_t)port_sys_get_ms();
    _bl_timeout_elapsed(&ctx->last_ms, 0);  /* 初始化活动时间 */

    /* 读取元数据 */
    bl_meta_t meta;
    _meta_load(&meta);
    if (meta.magic != BL_META_MAGIC || meta.boot_version != BL_META_VERSION) {
        /* 出厂/元数据损坏/烧了新版本 BootLoader：重置全部升级标志，
         * 避免旧版本残留的 pending/backup_valid/attempts 干扰新版本决策 */
        memset(&meta, 0, sizeof(meta));
        meta.magic = BL_META_MAGIC;
        meta.boot_version = BL_META_VERSION;
        _meta_store(&meta);
    }

    /* 升级请求：先把运行区当前固件备份到备份区，再进 IDLE 等命令 */
    if (meta.upgrade_req) {
        meta.upgrade_req = 0;
        if (_backup_run() == 0)
            meta.backup_valid = 1;
        _meta_store(&meta);
        ctx->state = BL_STATE_IDLE;
        return;
    }

    /* 新固件待确认：启动失败计数，超限则从备份区恢复旧固件 */
    if (meta.pending) {
        meta.attempts++;
        if (meta.attempts >= BL_MAX_ATTEMPTS) {
            if (meta.backup_valid && _restore_run() == 0) {
                /* 备份恢复成功：回滚到旧固件 */
                meta.pending      = 0;
                meta.backup_valid = 0;       /* 备份已消耗 */
            }
            meta.attempts     = 0;           /* 恢复成功或失败：重新计数，避免卡死 */
        }
        _meta_store(&meta);
    }

    /* 运行区有效：进 IDLE 等升级窗口（BL_IDLE_TIMEOUT_MS 后无命令才跳转）
     * 保证"有效但崩溃"的程序也能靠强行复位进入升级——复位后始终有 6s 烧录窗口 */
    if (bl_app_is_valid(BL_APP_RUN_START)) {
        ctx->state = BL_STATE_IDLE;
        return;
    }

    /* 运行区无效（损坏/恢复失败）：尝试从备份区恢复 */
    if (meta.backup_valid && _restore_run() == 0) {
        meta.backup_valid = 0;
        _meta_store(&meta);
        if (bl_app_is_valid(BL_APP_RUN_START)) {
            ctx->state = BL_STATE_IDLE;
            return;
        }
    }

    ctx->state = BL_STATE_IDLE;             /* 无可用固件：等升级 */
}

/* ============================================================
 * UDS 响应构建：统一产出 UDS PDU（负响应 7F+SID+NRC / 正响应 SID+0x40+data）
 * 两个传输通道共用，只是发送外壳不同
 * ============================================================ */
static void _build_uds_pdu(const uds_response_t *resp, uint8_t *payload, uint16_t *n)
{
    if (resp->nrc != UDS_NRC_POSITIVE) {
        payload[(*n)++] = 0x7FU;
        payload[(*n)++] = resp->data[0];    /* 原 SID（_nrc 存入 data[0]），UDS 标准线格式 */
        payload[(*n)++] = resp->nrc;
        return;
    }
    payload[(*n)++] = resp->sid;            /* 正响应：SID+0x40 + data */
    for (uint16_t i = 0; i < resp->data_len && *n < 64; i++)
        payload[(*n)++] = resp->data[i];
}

/* 传输壳：只做"把构建好的 UDS PDU 发出去"，具体封装由通道决定 */
typedef void (*resp_sender_t)(const uint8_t *pdu, uint16_t len);

/* CAN-FD 通道：ISO-TP 分段发送（自动单帧/多帧） */
static void _can_send_pdu(const uint8_t *pdu, uint16_t len)
{
    tf_send_pdu(BL_CANFD_RESPONSE_ID, pdu, len);
}

/* UART 通道：UDS PDU 包进 Data_User_Defined 帧（LTM 协议） */
static void _uart_send_pdu(const uint8_t *pdu, uint16_t len)
{
    ltm_commut_send(Data_User_Defined, (uint8_t *)pdu, len);
}

/* 完整 UDS PDU 处理：解析请求 → 服务 → 原通道回响应 → 状态联动 */
static void _handle_pdu(const uint8_t *pdu, uint16_t pdu_len, resp_sender_t send_resp)
{
    uds_request_t req;
    uds_response_t resp;
    memset(&req, 0, sizeof(req));

    /* 仅带子功能的服务（会话/安全/复位）才有 subfunc 字段；
     * 其余服务（0x22/0x34/0x36/0x37）第 2 字节起是数据参数，不能剥离 */
    req.sid = pdu[0];
    uint16_t data_len = 0;
    if (req.sid == UDS_SID_DIAGNOSTIC_SESSION ||
        req.sid == UDS_SID_SECURITY_ACCESS    ||
        req.sid == UDS_SID_ECU_RESET) {
        if (pdu_len >= 2) req.subfunc = pdu[1];
        data_len = (uint16_t)(pdu_len > 1 ? pdu_len - 2 : 0);
        if (data_len > 0 && data_len <= sizeof(req.data))
            memcpy(req.data, &pdu[2], data_len);
    } else {
        req.subfunc = 0;
        data_len = (uint16_t)(pdu_len > 0 ? pdu_len - 1 : 0);
        if (data_len > sizeof(req.data)) data_len = sizeof(req.data);
        if (data_len > 0)
            memcpy(req.data, &pdu[1], data_len);
    }
    req.data_len = data_len;

    uds_handle_request(&req, &resp);

    uint8_t payload[64];
    uint16_t n = 0;
    _build_uds_pdu(&resp, payload, &n);     /* PDU 只构建一次，传输壳只负责发送 */
    send_resp(payload, n);

    /* 状态联动：通过查询接口，不摸内部结构 */
    if (uds_is_programming())
        s_bl.state = BL_STATE_PROGRAM;
    else
        s_bl.state = BL_STATE_IDLE;

    _bl_timeout_elapsed(&s_bl.last_ms, 0);  /* 收到命令：刷新活动时间 */
}

/* ============================================================
 * 主循环处理
 * ============================================================ */
void bl_process(void)
{
    bl_context_t *ctx = &s_bl;

    /* JUMP 状态：立即跳转 */
    if (ctx->state == BL_STATE_JUMP) {
        bl_jump_to_app(ctx->app_start_addr);
        return;
    }

    /* RUN LED 心跳：BootLoader 运行期间 1s 翻转（显示层独立计时） */
    if (_bl_timeout_elapsed(&s_led_last_ms, 1000))
        port_led_toggle(PORT_LED_RUN);

    /* ==================== CAN-FD 通道：ISO-TP 重组 + UDS 服务 ==================== */
#if BL_ENABLE_UDS
    tf_poll();                                                      /* 推进多帧发送分段/超时 */
    while (port_canfd_recv(&ctx->rx_id, ctx->rx_buf, &ctx->rx_len) == 1) {
        if (tf_feed(ctx->rx_buf, ctx->rx_len)) {                    /* 单帧直达 / 多帧重组完成 */
            uint16_t pdu_len = 0;
            const uint8_t *pdu = tf_get_pdu(&pdu_len);
            _handle_pdu(pdu, pdu_len, _can_send_pdu);               /* 完整 PDU → UDS 服务 → 原通道回响应 */
        }
    }
#endif

    /* ==================== IAP 通道：UART + LTM 协议 ==================== */
#if BL_ENABLE_IAP
    uint8_t data_type;
    uint8_t payload[128];
    uint16_t payload_len = 0;
    if (ltm_commut_process(&data_type, payload, &payload_len)) {
        if (data_type == Data_User_Defined) {
            /* Data_User_Defined 载荷即 UDS PDU：同一服务，UART 外壳 */
            _handle_pdu(payload, payload_len, _uart_send_pdu);
        }
        /* 其他数据类型（电机指令等）BootLoader 不处理，忽略 */
    }
#endif

    /* IDLE 超时：无指令跳转 App（若有有效） */
    if (ctx->state == BL_STATE_IDLE) {
        if (_bl_timeout_elapsed(&ctx->last_ms, BL_IDLE_TIMEOUT_MS)) {
            if (bl_app_is_valid(ctx->app_start_addr))
                ctx->state = BL_STATE_JUMP;     /* App 有效：跳转 */
            /* App 无效：节流器已刷新时间戳，继续等待 */
        }
    }

    /* 编程会话接收超时：回默认会话 */
    if (ctx->state == BL_STATE_PROGRAM) {
        if (_bl_timeout_elapsed(&ctx->last_ms, BL_RX_TIMEOUT_MS)) {
            uds_abort_download();           /* 中止下载，回默认会话 */
            ctx->state = BL_STATE_IDLE;
        }
    }
}
