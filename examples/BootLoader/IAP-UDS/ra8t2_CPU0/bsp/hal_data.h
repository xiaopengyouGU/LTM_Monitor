/* generated HAL header - BootLoader 精简版（仅 CAN-FD） */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include "bsp_api.h"
#include "common_data.h"
#include "r_canfd.h"
#include "r_can_api.h"
#include "r_sci_b_uart.h"
FSP_HEADER
#define CANFD_CFG_AFL_CH1_RULE_NUM (16)
extern const can_instance_t g_canfd0;
extern canfd_instance_ctrl_t g_canfd0_ctrl;
extern const can_cfg_t g_canfd0_cfg;
extern const canfd_extended_cfg_t g_canfd0_cfg_extend;
void canfd0_callback(can_callback_args_t *p_args);
extern const uart_instance_t g_uart0;
extern sci_b_uart_instance_ctrl_t g_uart0_ctrl;
extern const uart_cfg_t g_uart0_cfg;
extern const sci_b_uart_extended_cfg_t g_uart0_cfg_extend;
void uart_callback(uart_callback_args_t *p_args);
FSP_FOOTER
#endif /* HAL_DATA_H_ */
