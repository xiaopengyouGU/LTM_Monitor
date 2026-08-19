# 统一 BootLoader SDK（UDS over CAN-FD + IAP over UART）

## 一、定位

在 RA8T2 上实现一个**双通道升级 BootLoader**：UDS 和 IAP 共用同一套升级服务与分区模型，只差传输壳。

| 通道 | 传输层 | 帧格式 | 设备要求 | 速率 |
|------|--------|--------|----------|------|
| UDS | CAN-FD（1M/2Mbps） | ISO-TP 重组/分段 | USB-CAN-FD 设备 | 高（64B/帧） |
| IAP | UART（SCI9, 115200） | LTM 协议（`0xA5B9`+CRC16） | USB-TTL，几块钱 | 中（126B/帧） |

**升级服务只有一份**（`uds_server`，UDS 兼容），两个通道的载荷即 UDS PDU：

- CAN-FD 通道：ISO-TP 重组后直接处理，响应走 ISO-TP 分段；
- UART 通道：`Data_User_Defined` 帧的载荷就是 UDS PDU，响应原样包回 LTM 帧。

因此"CAN 传一半、UART 接着传"都能工作——会话/块序号/CRC 是同一份状态。

## 二、目录结构

```
ra8t2_CPU0/
├── core/
│   ├── bootloader.c/h      # 状态机 + 元数据 + 运行区/备份区回滚（传输无关）
│   ├── uds_server.c/h      # UDS 兼容升级服务：会话/解锁/下载/CRC —— 唯一一份
│   ├── iso15765.c/h        # CAN-FD 通道：ISO-TP
│   └── protocol/           # UART 通道：LTM 协议（ltm_commut/protocol）
├── port/
│   ├── port_canfd.c/h      # CAN-FD 驱动（UDS 通道）
│   ├── port_uart.c/h       # UART 驱动（IAP 通道，FIFO 批量接收）
│   ├── port_flash.c/h      # MRAM 擦写（FSP 支持任意长度，无需 32B 对齐）
│   ├── port_sys.c/h        # 系统时钟/延时/跳转
│   └── port_led.c/h        # LED（ON_OFF 常亮 / RUN 1s 心跳）
├── app/                    # main.c + bootloader_config.h + 链接脚本
├── bsp/                    # 精简 FSP 支持：CAN-FD + SCI9 UART + MRAM + IO + 向量表
├── host/
│   ├── test/               # 平台无关测试：功能 + 压测（双通道 mock）
│   └── upgrade_client.py   # UART IAP 升级客户端（pyserial）
├── cmake/toolchain.cmake   # ARM 交叉编译工具链
├── script.py               # 一键构建/烧录/测试/UART 升级
└── build/                  # BootLoader.elf/hex/bin
```

## 三、分区与升级流程

```
0x02000000  Boot      32KB   ← 本 BootLoader（实际 15KB）
0x02008000  运行区    224KB   ← App 固定链接，只出 bin
0x02040000  备份区    224KB   ← 升级前自动备份旧固件
0x02078000  元数据    32KB    ← upgrade_req/pending/backup_valid/attempts
```

升级流程（两个通道命令完全一致）：

```
0x10 02 进入编程会话 → 0x27 安全解锁 → 0x34 请求下载(擦除)
→ 0x36 循环传输(块序号+数据) → 0x37 退出传输(CRC32 校验)
→ 0x11 01 复位跳转新固件
```

安全模型：升级前自动备份运行区到备份区；新固件启动后 App 调 `bl_commit_app()` 确认，未确认连续复位 `BL_MAX_ATTEMPTS` 次自动从备份区恢复旧固件。

### 升级窗口：藏在开机流程里，用户无感

**每次复位后 BootLoader 都先驻留 `BL_IDLE_TIMEOUT_MS`（6s）再跳转 App**，无论运行区程序是否有效。
这带来两个产品级能力：

1. **"有效但崩溃"的程序也能强制升级**：程序向量表完好、运行却崩溃时（刚烧的坏固件、异常逻辑），
   上位机无法通过 App 请求升级。此时强行复位即可——复位后 6 秒窗口内开始烧录，直接覆盖故障程序，
   不必等 `BL_MAX_ATTEMPTS` 次复位触发回滚。
2. **升级入口零成本**：不需要按键、不需要专用工具，物理复位就是升级入口，现场人员一根 USB-TTL 就能救活设备。

**为什么用户无感**：任何设备从上电到正常工作都要 10 秒以上——外设初始化、自检、通信握手，
哪一项都比 6 秒久。BootLoader 的升级窗口正好被这段"开机过程"完全覆盖，用户根本感知不到它的存在。
这是工业界成熟的做法：把升级窗口藏进启动流程，PLC/变频器/驱动器普遍如此。

> 代价是每次正常上电多等 6s 才进 App。若产品需要"上电秒起"，把 `BL_IDLE_TIMEOUT_MS` 调小即可（如 2000），
> 但会缩短强制升级窗口；两者取舍由产品形态决定。

## 四、UART 批量接收（相对 LTM_HAL uart.c 的改造）

LTM_HAL 原实现：`UART_EVENT_RX_CHAR` 逐字节回调 + `r_sci_b_uart_cfg.h` 中 `FIFO_SUPPORT=0`，**每个字节一次中断**，大固件连续流下 CPU 开销大。

本工程 `port_uart.c`：

1. `bsp/r_sci_b_uart_cfg.h` 开启 `SCI_B_UART_CFG_FIFO_SUPPORT (1)`，FSP 初始化 RX FIFO（触发深度 MAX）；
2. `bsp/vector_data.c` 把 SCI9 RXI 向量（14）替换为自写 `port_uart_rxi_isr`：一次中断把 FIFO 全部读出，**一次回调** `ltm_commut_recv(batch, n)`；
3. 短帧由硬件 15 ETU 空闲检测兜底产生 RXI，不丢帧；
4. TX 仍走 FSP `R_SCI_B_UART_Write` + 原 TXI/TEI 中断，零侵入。

## 五、编译开关（app/bootloader_config.h）

```c
#define BL_ENABLE_UDS   1   /* CAN-FD + ISO-TP 通道 */
#define BL_ENABLE_IAP   1   /* UART + LTM 协议通道 */
```

只需单通道版本时关掉另一个即可（bsp 仍编译两个外设，关闭的是轮询与初始化）。

## 六、构建与使用

```bat
python script.py -t                       :: 平台无关测试（功能 + 压测）
python script.py -b                       :: 构建 Release 固件
python script.py -R                       :: 构建 + J-Link 烧录
python script.py -R --keep-app            :: 构建 + 烧录，保留运行区旧 App
python script.py -u --port COM3 --firmware app.bin   :: UART IAP 升级
```

> **烧录默认擦除运行区**：J-Link 烧 BootLoader 后会在运行区起始（0x02008000）写入坏向量表
> （SP=0, PC=0），BootLoader 上电判定 App 无效 → **永久驻留等待升级，不跳转**。
> 适合串口供电场景（拔插 USB = 复位，重连上位机常超过 6s 升级窗口，旧 App 会导致"烧完又跳走"）。
> 需要保留运行区旧 App 时加 `--keep-app`。

App 侧接口（`core/bootloader.h`）：

```c
bl_request_upgrade();   /* App 请求升级：置 upgrade_req，复位后进入 BootLoader 等待 */
bl_commit_app();        /* App 启动成功后确认新固件有效 */
```

## 七、已知修正（相对 UDS 单通道版）

- `uds_request_t.data` 64B → 128B：UART 通道单块 100B 不再被截断（CAN 62B 未触发）；
- UDS 负响应线格式 `[7F, 原SID, NRC]`：原版取了 `resp->sid`（恒为 7F），现取 `data[0]`；
- `port_flash_write` 放宽 32B 对齐要求：FSP `R_MRAM_Write` 内部支持 <32B 编程线；
- bin 生成剥离 `__option_setting_*` 段，避免 13MB 假大文件（hex 保留供产线烧 option settings）。
- CAN-FD RX FIFO Payload Size（RFPLS）8B → 64B：RA8T2 硬件对超过 RFPLS 的帧只保留配置长度，13 字节的 0x34 请求（size 字段在第 9~12 字节）被截成 8 字节 → size=0 → NRC 0x31；`bsp/hal_data.c` 两个 FIFO 的 RFPLS 均改为 7（64B）。**任何 RA8T2 CAN-FD 接收路径都必须配 64B**。

> 链接未加 `-u _printf_float`：BootLoader 不调用 `ltm_commut_printf`，省掉浮点格式化支持（数 KB）。
> 若以后 BootLoader 需要 `%f` 文本输出，在 `CMakeLists.txt` 链接选项恢复该行即可。

> `core/protocol/protocol.c` 裁剪了调试 `printf`（`flag` 恒为 0，永不执行），
> 连带把 printf/vsnprintf 整套 stdio 机制从固件中移除，再省数 KB；帧格式与 LTM_HAL 字节级一致。

## 八、内存占用

`bsp/bsp_cfg.h` 覆盖 FSP 默认配置，压缩无用的栈/堆：

| 项目 | 默认 | BootLoader | 说明 |
|------|------|-----------|------|
| 主栈 `BSP_CFG_STACK_MAIN_BYTES` | 32KB | 2KB | 无 RTOS，最深调用链约 500B + ISR 嵌套，2KB 余量充足 |
| 堆 `BSP_CFG_HEAP_BYTES` | 16KB | 0B | BootLoader 无 malloc，彻底取消堆 |

固件实际占用：**flash 14.8KB（text 14917）/ RAM 4.4KB（bss 4435）**。
32KB Boot 分区余量 17KB+，936KB RAM 余量充足。

## 九、发布约定

**BootLoader 版本号与元数据版本号必须成对同步升级**（`app/bootloader_config.h`）：

```c
#define BL_VERSION_STRING   "1.1.0"
#define BL_META_VERSION     0x010100UL   /* 1.1.0 → 0x010100 */
```

- 元数据区（0x02078000）包含 `boot_version` 字段，`bl_init()` 检测到版本不匹配 →
  **重置全部升级标志**（upgrade_req / pending / backup_valid / attempts）；
- 效果：**烧录新版本 BootLoader 后第一次上电自动清空旧元数据**，
  旧版本残留状态不会干扰新版本决策——J-Link / 产线工具 / 任意烧录方式均生效（软件自愈）；
- 刻意设计：**同版本重烧不重置**——版本号未变时保留升级状态（如 pending、attempts），
  只有真正换了新 BootLoader 才清。因此每次发布新 BootLoader 务必同步更新这两个宏。
