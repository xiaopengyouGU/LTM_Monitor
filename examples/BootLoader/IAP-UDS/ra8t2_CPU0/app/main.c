/* 统一 BootLoader 入口：应用层只碰 bl_init / bl_process */
#include "bootloader.h"
#include "bootloader_config.h"
#include "port_canfd.h"
#include "port_uart.h"
#include "port_flash.h"
#include "port_led.h"
#include "port_sys.h"

int main(void)
{
    /* 精简外设初始化 */
    port_sys_init();
    port_led_init();
    port_flash_init();

#if BL_ENABLE_UDS
    port_canfd_init();
    /* CAN-FD 白名单：上位机可能从多个 ID 发帧，全部加入接收 */
    const uint16_t rx_ids[] = BL_CANFD_RX_ID_LIST;
    for (uint32_t i = 0; i < sizeof(rx_ids)/sizeof(rx_ids[0]); i++)
        port_canfd_set_filter(rx_ids[i]);
#endif

#if BL_ENABLE_IAP
    port_uart_init();
#endif

    /* BootLoader 主状态机（内部完成升级标志检查、协议对象初始化） */
    bl_init();

    while (1) {
        bl_process();       /* 双通道轮询：CAN-FD(UDS) + UART(IAP) + 状态流转 */
    }
}
