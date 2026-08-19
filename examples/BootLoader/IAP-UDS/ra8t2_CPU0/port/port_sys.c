#include "port_sys.h"
#include "bootloader_config.h"
#include "port_uart.h"
#include "port_canfd.h"
#include "bsp_api.h"
#include "r_ioport.h"
#include "hal_data.h"
#include "core_cm85.h"

volatile static uint64_t s_tick_ms = 0;

void SysTick_Handler(void)
{
    s_tick_ms++;
}

void port_sys_init(void)
{
    SystemInit();
    R_IOPORT_Open(&g_ioport_ctrl, &g_bsp_pin_cfg);

    uint32_t period = SystemCoreClock / 1000;
    SysTick_Config(period);
}

uint64_t port_sys_get_ms(void)
{
    return s_tick_ms;
}

void port_sys_delay_ms(uint16_t ms)
{
    R_BSP_SoftwareDelay(ms, BSP_DELAY_UNITS_MILLISECONDS);
}

void port_sys_jump(uint32_t app_addr)
{
    /* 跳转标记：App 启动据此恢复全局中断（软跳转不复位 PRIMASK） */
    *(volatile uint32_t *)BL_JUMP_FLAG_ADDR = BL_JUMP_MAGIC;

    __disable_irq();

    /* 关闭 BootLoader 打开的外设：让 App 从干净的硬件状态重新初始化（走 port 层隔离） */
    port_uart_close();
    port_canfd_close();

    SysTick->CTRL = 0;

    uint32_t msp = *(volatile uint32_t *)app_addr;
    uint32_t pc  = *(volatile uint32_t *)(app_addr + 4);

    __set_MSP(msp);
    SCB->VTOR = app_addr;

    typedef void (*app_entry_t)(void);
    app_entry_t entry = (app_entry_t)pc;
    entry();
}
