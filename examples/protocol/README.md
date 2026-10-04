# LTM 通讯协议（协议本体）

上位机与下位机之间的通讯协议实现，纯 C、无第三方依赖，可直接移植到 ARM Cortex-M 等平台。

| 文件 | 说明 |
|---|---|
| `protocol.c` / `protocol.h` | 帧格式与编解码：帧头(2) + 类型(1) + 长度(1) + 数据(N) + CRC16(2)，单帧 ≤134B |
| `ltm_commut.c` / `ltm_commut.h` | 应用层接口：数据类型定义、打包/解包与用户回调 |

移植只需提供字节收发（UART / CAN-FD 承载均可），其余无需改动。完整移植示例见 `examples/STM32`、
`examples/Renesas`，BootLoader 示例见 `examples/BootLoader`。

## 许可

本目录代码以 **MIT** 许可发布（见 `LICENSE`），可以自由集成到闭源固件，只需保留版权与许可声明。
仓库中的上位机（`src/`）等部分为 GPLv3。