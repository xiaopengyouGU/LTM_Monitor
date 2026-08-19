/* generated common header - BootLoader 精简版 */
#ifndef COMMON_DATA_H_
#define COMMON_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "r_mram.h"
#include "r_flash_api.h"
#include "r_ioport.h"
#include "bsp_pin_cfg.h"
FSP_HEADER
extern const flash_instance_t g_mram0;
extern mram_instance_ctrl_t g_mram0_ctrl;
extern const flash_cfg_t g_mram0_cfg;
#define IOPORT_CFG_NAME g_bsp_pin_cfg
#define IOPORT_CFG_OPEN R_IOPORT_Open
#define IOPORT_CFG_CTRL g_ioport_ctrl
extern const ioport_instance_t g_ioport;
extern ioport_instance_ctrl_t g_ioport_ctrl;
void g_common_init(void);
FSP_FOOTER
#endif /* COMMON_DATA_H_ */