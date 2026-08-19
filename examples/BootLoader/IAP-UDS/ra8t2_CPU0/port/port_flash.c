#include "port_flash.h"
#include "bootloader_config.h"

#include "hal_data.h"
#include "r_mram.h"

/* ============================================================
 * MRAM 精简驱动（RA8T2 代码存储，直接调 FSP R_MRAM）
 * 实例 g_mram0 由 BootLoader 精简 hal_data 提供
 * ============================================================ */
static flash_ctrl_t * s_mram_ctrl = &g_mram0_ctrl;

int port_flash_init(void)
{
    fsp_err_t err = R_MRAM_Open(s_mram_ctrl, &g_mram0_cfg);
    return (err == FSP_SUCCESS) ? 0 : -1;
}

int port_flash_erase(uint32_t addr, uint32_t size)
{
    if (size == 0) return -1;
    uint32_t blocks = (size + BL_MRAM_ERASE_BLOCK - 1) / BL_MRAM_ERASE_BLOCK;
    fsp_err_t err = R_MRAM_Erase(s_mram_ctrl, addr, blocks);
    return (err == FSP_SUCCESS) ? 0 : -1;
}

int port_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (len == 0) return 0;
    /* FSP R_MRAM_Write 内部支持任意长度（<32B 时走行内 FLUSH 流程），
     * 无需强制 32B 对齐，0x36 传输块长度可自由选择（UDS 62B / IAP 100B 等） */
    fsp_err_t err = R_MRAM_Write(s_mram_ctrl, (uint32_t)data, addr, len);
    return (err == FSP_SUCCESS) ? 0 : -1;
}

int port_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (!data || len == 0) return -1;
    for (uint32_t i = 0; i < len; i++)
        data[i] = *(volatile uint8_t *)(addr + i);
    return 0;
}

uint32_t port_flash_get_write_unit(void)
{
    return BL_MRAM_WRITE_UNIT;
}
