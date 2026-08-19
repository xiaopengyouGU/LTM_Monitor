# LTM_Monitor 架构说明

这份文档讲三件事：项目怎么分层、数据在线程之间怎么流、代码里有哪些约定。
读代码之前先过一遍，比对着文件猜快很多。

## 1. 分层

```
application   main.cpp（入口，只 new MainWindow）
ui_widgets    MainWindow（壳）+ DataHub（数据中转站）+ 各业务 Widget
components    可独立复用的组件（无业务线程）
                protocol / chart_map / data_map / uds_server / status_bar
modules       传输层与核心能力库
                serial / canfd / chart / record
```

依赖只从上往下，同层之间尽量不互相依赖。components 不依赖 UI 和传输层，
其中 protocol / chart_map / data_map / uds_server 是纯逻辑，可以单独编译测试。

各模块职责一句话：

| 模块 | 职责 |
|---|---|
| modules/chart | 图表核心：DataStorage 环形存储 / ChartManager 门面 / ChartController 渲染适配 / 导入导出 / FFT / 降采样 |
| modules/serial | 串口纯字节收发，不感知任何协议 |
| modules/canfd | CAN-FD 纯帧收发：Controller 隔离 zcan 硬件差异，Worker 周期轮询 |
| modules/record | 日志 + 数据库，全部异步落盘，不阻塞 UI |
| components/protocol | LTM / Modbus 打包拆包、环形缓冲、CRC16、Modbus 主站事务器，全部 Pimpl，与介质无关 |
| components/chart_map | 数据源信号 → 图表通道的统一映射表 |
| components/data_map | DBC / 自定义 JSON 协议按位解码 |
| components/uds_server | 硬件无关的 UDS 升级协议引擎（与 BootLoader 状态机镜像） |
| components/status_bar | 状态栏控件，供各 Widget 直连注入 |
| ui_widgets/main_window | 壳 + 全部接线；DataHub 是唯一数据中转站 |

## 2. 线程模型与数据流

项目共 5 个执行上下文 + 1 个线程池：

| 执行上下文 | 归属 | 职责 |
|---|---|---|
| UI 主线程 | MainWindow / 全部 Widget / ChartManager 刷新定时器 | 界面、绘图、广播 |
| data_thread | DataHub | 高频数据加工与分发 |
| 串口线程 | SerialWorker | 串口读写 |
| CAN-FD 线程 | CanfdWorker | 20ms 周期轮询收帧（升级时切 3ms） |
| 记录线程 | RecordWorker / LogWorker | 日志与数据库异步落盘 |
| QtConcurrent 线程池 | ChartManager 刷新任务 | 并行取数 / 降采样 / FFT |

数据流全景：

```
串口设备 ──> SerialWorker（只转字节）
                 │ serialDataUpdated
                 ▼
            DataHub(data_thread) ──按模式分发 LTM / Common / Modbus──> textOrCMDReceived ──> 控制台/状态栏
                 │ channelActualChanged（节流）──> ChartDialog 实际值 / PidWidget

CAN-FD 设备 ──> CanfdWorker（只转帧）
                 │ canfdDataUpdated
                 ▼
            DataHub(data_thread)：缓冲 ──> 排水 ──> LTM-over-CANFD（0x101 上行）解析 / DataMap 解码
                 │ canfdRowsReceived ──> CanfdWidget 表格
                 │ channelActualChanged ──> 图表通道
                 │ canfdRawReceived ──> UdsWidget（仅升级模式）
                 │ canfdDropped ──> 状态栏/日志

DataHub ──addData──> ChartManager(UI线程) ──> DataStorage
                 │ 刷新定时器（默认 100ms）
                 ▼
            QtConcurrent::mapped 并行取数/降采样/FFT
                 │ QFutureWatcher::finished（主线程收尾）
                 ▼
            ChartController ──> QCustomPlot
```

要点：

- 高频数据不在 UI 线程加工。解析、格式化、解码都在 data_thread 完成，UI 只消费成品。
- 跨线程一律走信号槽队列连接，没有跨线程共享内存，不需要显式锁。
- 图表刷新是唯一"UI 线程发起 + 线程池干活 + 异步收尾"的路径，主线程零阻塞。
- LTM 协议与介质无关：串口和 CAN-FD 共用同一套解析。发送路由串口优先，串口不在线时
  组装 CAN-FD 0x100 帧；0x100 下行 / 0x101 上行收发分 ID，多电机总线不冲突。
- 升级模式是 DataHub 的一个开关（setUpgradeMode）：只把原始帧喂 UdsWidget，表格/图表自动旁路。

## 3. 代码里的约定

这些约定散落在注释里，是项目能跑起来的前提：

1. **时间戳必须单调递增**。DataStorage 的环形缓冲和范围查找都依赖它，`times.last()` 就是最新时间。
   破坏单调性等于未定义行为。
2. **每通道同一时刻只有一个写者**，就是 DataHub。读侧（刷新/导出/FFT）可以并发；读写锁只在批量导入时短暂持有。
3. **getData 按值返回 + QList 隐式共享**。拷贝是浅拷贝（引用计数 +1），深拷贝只在写的时候发生（COW detach）。
   相对时间平移要显式 `shiftedTimes = data.times` 再改，把 detach 留在并行线程里，主线程广播才零拷贝。
4. **环形缓冲下标用条件减法，不用取模**。`head + row < 2 * capacity` 恒成立，减法比取模快一个量级。
5. **降采样阈值上限留 2**：`threshold = qMin(threshold, LTTB_THRESHOLD - 2)`，余量给 M4 的首尾保留点。
6. **dataVersion 写入/清空递增**，刷新时对比版本号去重——比比对最新时间戳可靠（时间戳可能重复）。
7. **CAN-FD 轮询周期**：正常 20ms，UDS 升级期间 3ms（MIN_POLL_TIME = 2ms），升级结束在 finish() 恢复。
8. **表格行格式化在源头做一次**（CanfdFrameRow::fromFrame），模型只存字符串，data() 零加工零分支。

## 4. 关键性能决策

| 决策 | 原因 |
|---|---|
| 全项目 Pimpl | 头文件 = 纯接口面，编译隔离，实现细节不泄漏 |
| DataStorage 环形缓冲预分配（20 万点/通道） | 写入 O(1)，不扩容、不搬移；超限覆盖最旧 |
| M4 降采样首尾点单独保留、桶内极值按 x 序追加 | 桶内 min/max 乱拼会产生折返锯齿——V0.2 毛刺的根因 |
| LTTB 上限 4500 且留 2 | 数据量限制保证容量足够，留 2 给 M4 首尾点 |
| FFT 复用 raw.times 作虚部 | 原地变换不额外分配；N 向下取 2 的幂，幅度谱只算前半段，省近一半运算 |
| 平均采样率由相邻时间戳估算（(n-1)/ΣΔt） | 非均匀采样下的最佳近似，rate_N 预计算避免每 bin 一次除法 |
| 汉宁窗 ×2 补偿 | 相干增益 0.5，乘 2 后峰值幅度与矩形窗一致 |
| 并行取数 + QFutureWatcher 异步收尾 | 每通道一个任务，主线程零阻塞，单通道失败隔离 |
| 相对时间平移在并行任务内完成 | COW detach 发生在工作线程，主线程广播零拷贝 |
| 导入/导出按列批量处理 | 单次加锁，避免逐点加锁 |
| 刷新去重用 dataVersion | 无新数据时不触发全量取数 |
| 升级轮询 20ms → 3ms | 缩短升级耗时，结束即恢复 |

## 5. 用到的设计模式

- **Pimpl（桥接）**：全项目 40+ 类，用得最多。头文件只剩接口，实现随便改。
- **观察者**：Qt 信号槽，事件分发，无处不在。
- **中介者**：DataHub，多数据源汇聚、多消费者解耦、升级旁路开关。
- **门面**：ChartManager / SerialManager / CanfdManager / RecordManager / UdsServer，对外只留一个入口。
- **工作对象**：SerialWorker / CanfdWorker / RecordWorker，moveToThread 线程化。
- **生产者-消费者**：工作线程 → 信号槽队列 → data_thread / UI，跨线程无锁数据流。

## 6. 代码量分布（不含 third_party / 压测 / .ui）

总计约 **10736 行 / 84 文件**：

| 分层 | 行数 | 占比 |
|---|---|---|
| modules | 4633 | 43% |
| ui_widgets | 4176 | 39% |
| components | 1911 | 18% |
| application | 16 | 0% |

大头在 modules（驱动隔离、图表性能、异步落盘）和 ui_widgets（接线 + DataHub）；
components 行数最少，但协议解析和映射都在这。

## 7. 阅读顺序

1. 本文档，先把线程和数据流这张图刻进脑子。
2. application/main.cpp，几行。
3. ui_widgets 的头文件（只看 .h）——全是 Pimpl，头文件就是架构图，接口、信号、槽一目了然。
4. mainwindow.cpp 的 build*() 系列——看接线，壳有多薄。
5. data_hub.cpp——数据加工和分发都在这里。
6. components——纯逻辑无 UI 无线程，最好读，先建立协议和映射的概念。
7. modules/serial → modules/canfd——驱动封装 + 协议承载。
8. modules/chart（最后读）——DataStorage（存储+降采样）→ ChartManager（门面+并行刷新）→ ChartController（渲染）。
9. modules/record——异步落盘。
10. chart/tests 压测——性能基线。

边读边把线程时序和数据流画下来，比反复翻代码有用。

## 8. 与固件端的对齐

LTM_Monitor 不是孤立的上位机，和 LtMotorLib 生态严格对应：

- **LTM 协议**：examples/protocol/（纯 C，可移植到任意 MCU），上位机 protocol 组件与固件端 ltm_commut 同构。
- **UDS 升级**：上位机 UdsServer 与 BootLoader（LtMotorLib/BootLoader/IAP-UDS）状态机严格对齐；
  CAN-FD 通道走 UDS 协议，串口通道走 LTM 协议（IAP）。
- **数据语义**：CAN-FD 帧用设备 µs 时间戳（应用层保存基准），串口帧用主机时间戳；
  图表只关心"单调递增"，不关心时钟来源。
