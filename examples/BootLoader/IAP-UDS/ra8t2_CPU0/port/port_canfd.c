#include "port_canfd.h"
#include "bootloader_config.h"

#include "hal_data.h"
#include "r_canfd.h"

#include <string.h>

/* ============================================================
 * CAN-FD 精简驱动（参考 LTM_HAL canfd.c 写法，BootLoader 自包含）
 * 只依赖 FSP：R_CANFD + hal_data 实例，不依赖 LTM_HAL
 * ============================================================ */
static canfd_instance_ctrl_t * s_canfd_ctrl = &g_canfd0_ctrl;
static can_cfg_t             * s_canfd_cfg  = &g_canfd0_cfg;
static can_frame_t             s_tx_frame;
static can_frame_t             s_rx_frame;
static volatile uint8_t        s_tx_cplt = 1;
static volatile uint8_t        s_rx_cplt = 0;

#define BL_FILTER_MAX   16
typedef struct {
    uint16_t ids[BL_FILTER_MAX];
    uint8_t  count;
    uint8_t  enable;
} bl_canfd_filter_t;

/* CAN-FD 接收过滤规则表（hal_data 引用）：接收 FIFO0，标准帧，全通 */
const canfd_afl_entry_t p_canfd0_afl[CANFD_CFG_AFL_CH1_RULE_NUM] =
{
    {
        .id = { .id = 0x000, .frame_type = CAN_FRAME_TYPE_DATA, .id_mode = CAN_ID_MODE_STANDARD },
        .mask = { .mask_id = 0x000, .mask_frame_type = 1, .mask_id_mode = 1 },
        .destination = { .minimum_dlc = CANFD_MINIMUM_DLC_0, .rx_buffer = CANFD_RX_MB_NONE,
                         .fifo_select_flags = CANFD_RX_FIFO_0 },
    },
};

static bl_canfd_filter_t s_filter;
static void (*s_rxcall)(void) = NULL;

static const uint8_t s_dlc_to_len[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
};

static uint8_t _len_map_dlc(uint16_t *length);
static int     _filter_check(uint16_t id);

void port_canfd_init(void)
{
    R_CANFD_Open(s_canfd_ctrl, s_canfd_cfg);
    memset(&s_filter, 0, sizeof(s_filter));
    s_tx_cplt = 1;
    s_rx_cplt = 0;
}

void port_canfd_close(void)
{
    R_CANFD_Close(s_canfd_ctrl);
    s_tx_cplt = 1;
    s_rx_cplt = 0;
}

void port_canfd_set_filter(uint16_t id)
{
    if (s_filter.count >= BL_FILTER_MAX) return;
    for (int i = 0; i < s_filter.count; i++)
        if (s_filter.ids[i] == id) return;
    s_filter.ids[s_filter.count++] = id;
    s_filter.enable = 1;
}

void port_canfd_send(uint16_t id, const uint8_t *buf, uint16_t len)
{
    if (!buf || !len)        return;
    if (!s_tx_cplt)          return;

    uint16_t out_len = len;
    uint8_t dlc = _len_map_dlc(&out_len);

    memset(&s_tx_frame, 0, sizeof(s_tx_frame));
    s_tx_frame.id                  = id;
    s_tx_frame.id_mode             = CAN_ID_MODE_STANDARD;
    s_tx_frame.type                = CAN_FRAME_TYPE_DATA;
    s_tx_frame.options             = CANFD_FRAME_OPTION_BRS | CANFD_FRAME_OPTION_FD;
    s_tx_frame.data_length_code    = out_len;
    memcpy(s_tx_frame.data, buf, len);
    if (out_len > len)             /* DLC 向上取整，补 0 */
        memset(&s_tx_frame.data[len], 0, out_len - len);

    s_tx_cplt = 0;
    R_CANFD_Write(s_canfd_ctrl, CANFD_TX_MB_0, &s_tx_frame);
}

uint8_t port_canfd_recv(uint16_t *id, uint8_t *buf, uint16_t *len)
{
    if (!id || !buf || !len) return 0;
    if (!s_rx_cplt)          return 0;

    *id  = s_rx_frame.id;
    *len = s_rx_frame.data_length_code;     /* FSP：data_length_code 即实际字节数 */
    memcpy(buf, (uint8_t *)s_rx_frame.data, *len);
    s_rx_cplt = 0;
    return 1;
}

void port_canfd_set_rxcall(void (*cb)(void))
{
    s_rxcall = cb;
}

/* FSP 回调：由 hal_data 的 canfd0_callback 绑定 */
void canfd0_callback(can_callback_args_t *p_args)
{
    switch (p_args->event)
    {
        case CAN_EVENT_BUS_RECOVERY:
        case CAN_EVENT_ERR_BUS_LOCK:
        case CAN_EVENT_TX_ABORTED:
        case CAN_EVENT_ERR_BUS_OFF:
        case CAN_EVENT_ERR_GLOBAL:
        case CAN_EVENT_ERR_CHANNEL:
        case CAN_EVENT_TX_COMPLETE:
            s_tx_cplt = 1;
            break;
        case CAN_EVENT_RX_COMPLETE:
            if (!_filter_check(p_args->frame.id)) return;
            memcpy(&s_rx_frame, &p_args->frame, sizeof(can_frame_t));
            s_rx_cplt = 1;
            if (s_rxcall) s_rxcall();
            break;
        default:
            break;
    }
}

/* ============ 内部函数 ============ */
static uint8_t _len_map_dlc(uint16_t *length)
{
    uint16_t len = *length;
    if (len <= 8)   return (uint8_t)len;
    if (len > 64) { *length = 64; return 15; }
    uint8_t dlc = 8 + ((len - 8 + 3) >> 2);
    if (dlc > 15) dlc = 15;
    *length = (uint16_t)s_dlc_to_len[dlc];
    return dlc;
}

static int _filter_check(uint16_t id)
{
    if (!s_filter.enable) return 1;
    if (!s_filter.count)  return 1;
    for (int i = 0; i < s_filter.count; i++)
        if (s_filter.ids[i] == id) return 1;
    return 0;
}
