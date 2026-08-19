#include "port_uart.h"
#include "bootloader_config.h"
#include "port_sys.h"

#include "hal_data.h"
#include "bsp_api.h"

/* ============================================================
 * SCI9 UART 精简驱动（自包含，仅依赖 FSP：R_SCI_B_UART + hal_data）
 *
 * 批量接收原理：
 *   1. bsp/r_sci_b_uart_cfg.h 开启 FIFO_SUPPORT=1，FSP 初始化 RX FIFO（触发深度 MAX）；
 *   2. bsp/vector_data.c 把 SCI9 RXI 向量替换为本文件 port_uart_rxi_isr：
 *      一次中断把 FIFO 全部读出到批量缓冲，一次回调 ltm_commut_recv(batch, n)；
 *   3. 短帧（不足触发深度）由硬件 15 ETU 空闲检测兜底产生 RXI，不丢帧；
 *   4. TX 仍走 FSP R_SCI_B_UART_Write + FSP TXI/TEI 中断。
 * ============================================================ */

#define PORT_UART_RX_BATCH  16      /* SCI9 RX FIFO 深度（RTRG=MAX 时单次批量上限） */

static uart_ctrl_t *s_ctrl = &g_uart0_ctrl;
static volatile uint8_t s_tx_cplt = 1;
static void (*s_rxfeed)(uint8_t *buf, uint16_t len) = NULL;

void port_uart_init(void)
{
    fsp_err_t err = R_SCI_B_UART_Open(s_ctrl, &g_uart0_cfg);
    (void)err;
    s_tx_cplt = 1;
}

void port_uart_close(void)
{
    R_SCI_B_UART_Close(s_ctrl);
    s_tx_cplt = 1;
}

void port_uart_send(uint8_t *buf, uint16_t len)
{
    if (!buf || !len) return;
    /* 等待上一帧发送完成（带超时）：避免 TX 完成标志异常时后续打印/响应被静默丢弃 */
    uint32_t start = (uint32_t)port_sys_get_ms();
    while (!s_tx_cplt) {
        if ((uint32_t)port_sys_get_ms() - start >= 50) break;
    }
    s_tx_cplt = 0;
    R_SCI_B_UART_Write(s_ctrl, buf, len);
}

void port_uart_set_rxfeed(void (*feed)(uint8_t *buf, uint16_t len))
{
    s_rxfeed = feed;
}

/* FSP 驱动回调（hal_data g_uart0_cfg.p_callback）：仅发送完成事件 */
void uart_callback(uart_callback_args_t *p_args)
{
    if (p_args->event == UART_EVENT_TX_COMPLETE)
        s_tx_cplt = 1;
}

/* ============================================================
 * 自写 SCI9 RXI 中断：批量接收核心
 * 一次中断把 RX FIFO 全部读出，一次性回调协议层
 * ============================================================ */
void port_uart_rxi_isr(void)
{
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    R_BSP_IrqStatusClear(irq);          /* 清分组中断状态，允许下次 RXI */

    uint8_t batch[PORT_UART_RX_BATCH];
    uint16_t n = 0;
    while (n < PORT_UART_RX_BATCH && (R_SCI_B9->FRSR_b.R > 0U))
        batch[n++] = (uint8_t)R_SCI_B9->RDR_BY;

    R_SCI_B9->CFCLR |= R_SCI_B0_CFCLR_RDRFC_Msk;   /* 清 RDRF，允许下次 RXI 边沿 */

    if (n && s_rxfeed)
        s_rxfeed(batch, n);             /* 一次性喂协议层环形缓冲 */
}
