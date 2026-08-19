/* generated pin source - BootLoader 精简版（CAN1 + LED） */
#include "bsp_api.h"
#include "r_ioport.h"

const ioport_pin_cfg_t g_bsp_pin_cfg_data[] = {

		/* UART（IAP 通道）：SCI9 RXD=P208 / TXD=P209 */
		{ .pin = BSP_IO_PORT_02_PIN_08, .pin_cfg =
				((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
						| (uint32_t) IOPORT_PERIPHERAL_SCI1_3_5_7_9) },

		{ .pin = BSP_IO_PORT_02_PIN_09, .pin_cfg =
				((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
						| (uint32_t) IOPORT_PERIPHERAL_SCI1_3_5_7_9) },

		{ .pin = BSP_IO_PORT_04_PIN_14, .pin_cfg =
				((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
						| (uint32_t) IOPORT_PERIPHERAL_CAN) },

		{ .pin = BSP_IO_PORT_05_PIN_12, .pin_cfg =
				((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
						| (uint32_t) IOPORT_PERIPHERAL_CAN) },

		/* LED：STOP=P03_01 / ON_OFF=P03_02 / RUN=P03_08 / ERR=P09_02 */
		{ .pin = BSP_IO_PORT_03_PIN_01, .pin_cfg =
				((uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT
						| (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) },

		{ .pin = BSP_IO_PORT_03_PIN_02, .pin_cfg =
				((uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT
						| (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) },

		{ .pin = BSP_IO_PORT_03_PIN_08, .pin_cfg =
				((uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT
						| (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) },

		{ .pin = BSP_IO_PORT_09_PIN_02, .pin_cfg =
				((uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT
						| (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) },
};

const ioport_cfg_t g_bsp_pin_cfg =
        { .number_of_pins = sizeof(g_bsp_pin_cfg_data)/sizeof(g_bsp_pin_cfg_data[0]), .p_pin_cfg_data = &g_bsp_pin_cfg_data[0], .p_extend = NULL, };
