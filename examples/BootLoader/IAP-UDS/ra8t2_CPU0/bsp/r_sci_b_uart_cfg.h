/* generated configuration header file - IAP BootLoader（SCI9 UART，开启 FIFO 批量接收） */
#ifndef R_SCI_B_UART_CFG_H_
#define R_SCI_B_UART_CFG_H_
#ifdef __cplusplus
            extern "C" {
            #endif

#define SCI_B_UART_CFG_PARAM_CHECKING_ENABLE (BSP_CFG_PARAM_CHECKING_ENABLE)
#define SCI_B_UART_CFG_FIFO_SUPPORT (1)     /* 关键：开启 FIFO，配合自写 RXI 中断实现批量接收 */
#define SCI_B_UART_CFG_DTC_SUPPORTED (0)
#define SCI_B_UART_CFG_FLOW_CONTROL_SUPPORT (0)
#define SCI_B_UART_CFG_TX_ENABLE (1)
#define SCI_B_UART_CFG_RX_ENABLE (1)

#ifdef __cplusplus
            }
            #endif
#endif /* R_SCI_B_UART_CFG_H_ */
