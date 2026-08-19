/* generated vector header file - BootLoader 精简版（仅 CAN-FD 中断） */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H
#ifdef __cplusplus
        extern "C" {
        #endif
/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (9)
#endif
/* ISR prototypes */
void canfd_error_isr(void);
void canfd_channel_tx_isr(void);
void canfd_common_fifo_rx_isr(void);
void canfd_rx_fifo_isr(void);
void port_uart_rxi_isr(void);
void sci_b_uart_txi_isr(void);
void sci_b_uart_tei_isr(void);
void sci_b_uart_eri_isr(void);

#define VECTOR_NUMBER_CAN1_CHERR ((IRQn_Type) 18) /* CAN1 CHERR (Channel  error) */
#define CAN1_CHERR_IRQn          ((IRQn_Type) 18)
#define VECTOR_NUMBER_CAN1_TX ((IRQn_Type) 19) /* CAN1 TX (Transmit interrupt) */
#define CAN1_TX_IRQn          ((IRQn_Type) 19)
#define VECTOR_NUMBER_CAN1_COMFRX ((IRQn_Type) 20) /* CAN1 COMFRX (Common FIFO receive interrupt) */
#define CAN1_COMFRX_IRQn          ((IRQn_Type) 20)
#define VECTOR_NUMBER_CAN_GLERR ((IRQn_Type) 21) /* CAN GLERR (Global error) */
#define CAN_GLERR_IRQn          ((IRQn_Type) 21)
#define VECTOR_NUMBER_CAN_RXF ((IRQn_Type) 22) /* CAN RXF (Global receive FIFO interrupt) */
#define CAN_RXF_IRQn          ((IRQn_Type) 22)
#define VECTOR_NUMBER_SCI9_RXI ((IRQn_Type) 14) /* SCI9 RXI (Receive data full) */
#define SCI9_RXI_IRQn          ((IRQn_Type) 14)
#define VECTOR_NUMBER_SCI9_TXI ((IRQn_Type) 15) /* SCI9 TXI (Transmit data empty) */
#define SCI9_TXI_IRQn          ((IRQn_Type) 15)
#define VECTOR_NUMBER_SCI9_TEI ((IRQn_Type) 16) /* SCI9 TEI (Transmit end) */
#define SCI9_TEI_IRQn          ((IRQn_Type) 16)
#define VECTOR_NUMBER_SCI9_ERI ((IRQn_Type) 17) /* SCI9 ERI (Receive error) */
#define SCI9_ERI_IRQn          ((IRQn_Type) 17)
/* The number of entries required for the ICU vector table. */
#define BSP_ICU_VECTOR_NUM_ENTRIES (23)

#ifdef __cplusplus
        }
        #endif
#endif /* VECTOR_DATA_H */
