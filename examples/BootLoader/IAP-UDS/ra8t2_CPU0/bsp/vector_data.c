/* generated vector source file - BootLoader 精简版 */
#include "bsp_api.h"
#if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
            [14] = port_uart_rxi_isr,      /* SCI9 RXI: 自写批量接收 */
            [15] = sci_b_uart_txi_isr,     /* SCI9 TXI: FSP 发送 */
            [16] = sci_b_uart_tei_isr,     /* SCI9 TEI: FSP 发送完成 */
            [17] = sci_b_uart_eri_isr,     /* SCI9 ERI: FSP 错误处理 */
            [18] = canfd_error_isr,        /* CAN1 CHERR (Channel error) */
            [19] = canfd_channel_tx_isr,   /* CAN1 TX */
            [20] = canfd_common_fifo_rx_isr, /* CAN1 COMFRX */
            [21] = canfd_error_isr,        /* CAN GLERR */
            [22] = canfd_rx_fifo_isr,      /* CAN RXF */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [14] = BSP_PRV_VECT_ENUM(EVENT_SCI9_RXI,GROUP6),
            [15] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TXI,GROUP7),
            [16] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TEI,GROUP0),
            [17] = BSP_PRV_VECT_ENUM(EVENT_SCI9_ERI,GROUP1),
            [18] = BSP_PRV_VECT_ENUM(EVENT_CAN1_CHERR,GROUP2),
            [19] = BSP_PRV_VECT_ENUM(EVENT_CAN1_TX,GROUP3),
            [20] = BSP_PRV_VECT_ENUM(EVENT_CAN1_COMFRX,GROUP4),
            [21] = BSP_PRV_VECT_ENUM(EVENT_CAN_GLERR,GROUP5),
            [22] = BSP_PRV_VECT_ENUM(EVENT_CAN_RXF,GROUP6),
        };
        #endif
#endif
