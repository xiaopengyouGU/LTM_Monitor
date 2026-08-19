#include "port_led.h"
#include "bsp_api.h"
#include "r_ioport.h"
#include "hal_data.h"

/* LED 引脚表（与 pin_data.c 对应，参考 LTM_HAL led.c 写法） */
typedef struct {
    bsp_io_port_pin_t port_pin;
    uint8_t state;
} bl_led_config_t;

static bl_led_config_t s_led_cfg[PORT_LED_COUNT] = {
    [PORT_LED_STOP]   = { BSP_IO_PORT_03_PIN_01, 0 },
    [PORT_LED_RUN]    = { BSP_IO_PORT_03_PIN_08, 0 },
    [PORT_LED_ON_OFF] = { BSP_IO_PORT_03_PIN_02, 0 },
    [PORT_LED_ERR]    = { BSP_IO_PORT_09_PIN_02, 0 },
};

void port_led_init(void)
{
    /* 引脚方向/初始电平已在 pin_data 配置，无需额外操作 */
}

void port_led_set(port_led_id_t id, uint8_t state)
{
    if (id >= PORT_LED_COUNT) return;
    R_IOPORT_PinWrite(&g_ioport_ctrl, s_led_cfg[id].port_pin,
                      state ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
    s_led_cfg[id].state = state;
}

void port_led_toggle(port_led_id_t id)
{
    if (id >= PORT_LED_COUNT) return;
    uint8_t s = s_led_cfg[id].state;
    port_led_set(id, (uint8_t)(s ? 0 : 1));
}