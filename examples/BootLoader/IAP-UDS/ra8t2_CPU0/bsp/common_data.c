/* generated common source - BootLoader 精简版 */
#include "common_data.h"
mram_instance_ctrl_t g_mram0_ctrl;
const flash_cfg_t g_mram0_cfg = { .data_flash_bgo = false, .p_callback = NULL,
        .p_context = NULL,
        .err_irq = FSP_INVALID_VECTOR,
        .err_ipl = (BSP_IRQ_DISABLED), .irq = FSP_INVALID_VECTOR, .ipl =
                BSP_IRQ_DISABLED, };
const flash_instance_t g_mram0 = { .p_ctrl = &g_mram0_ctrl, .p_cfg =
        &g_mram0_cfg, .p_api = &g_flash_on_mram };
ioport_instance_ctrl_t g_ioport_ctrl;
const ioport_instance_t g_ioport = { .p_api = &g_ioport_on_ioport, .p_ctrl =
        &g_ioport_ctrl, .p_cfg = &g_bsp_pin_cfg, };
void g_common_init(void) {
}