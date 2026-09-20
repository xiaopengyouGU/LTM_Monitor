#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "status_bar.h"
#include "chart.h"
#include "canfd.h"
#include "serial.h"
#include "record.h"
#include "canfd_widget.h"
#include "uds_widget.h"
#include "serial_widget.h"
#include "console_widget.h"
#include "pid_widget.h"
#include "log_analysis.h"
#include "chart_dialog.h"
#include "data_hub.h"
#include "ltm_protocol.h"
#include "modbus_master.h"

#include <cstring>
#include <QThread>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QActionGroup>

#define LOG_DEBUG(msg)  {if(record_manager) record_manager->logDebug(msg);}
#define LOG_INFO(msg)   {if(record_manager) record_manager->logInfo(msg);}
#define LOG_WARN(msg)   {if(record_manager) record_manager->logWarn(msg);}
#define LOG_ERROR(msg)  {if(record_manager) record_manager->logError(msg);}

static void setLabelColor(QLabel *label, const QString &color);
static uint32_t parseCanfdBaud(const QString &s);

// ============================================================
// 私有实现（Pimpl）：UI 与全部成员/逻辑收敛于此
// ============================================================
class MainWindow::Private
{
public:
    explicit Private(MainWindow *main) : main(main) {}
    ~Private();

    void build();                       // 主窗口组装入口
    void buildUI_SerialPort();
    void buildUI_SerialWidgets();
    void buildUI_Canfd();
    void buildRecord();
    void buildChart();
    void build_DataHub();
    void buildUI_Others();

    // 槽实现（外层槽委托到这里）
    void doExportFinished(bool success, const QString &msg);
    void doSerialPortNumChanged(const QStringList &portNum);
    void doSerialOpened(bool success, const QString &msg);
    void doSerialClose();
    void doCanfdOpened(bool success, const QString &msg);
    void doCanfdClosed();
    void doCanfdError(const QString &msg);
    void doCanfdOnlineChanged(bool online);
    void doCanfdBusError(uint32_t errCode, int channel);
    void doCanfdDropped(int dropped, uint64_t total);
    void doTextOrCMDReceived(uint8_t type, const QString &str, const QByteArray &data);
    void onBtnStart();
    void onBtnSerial();
    void onBtnCanfd();
    void onBtnDataExport();
    void onBtnChartShow();
    void onBtnMode();
    void onBtnReset();
    void onBtnClearShow();
    void onBtnStopShow();
    void onActImportDB();
    void onActOpenLog();
    void onActUseIntro();
    void onActShowCanfd(bool checked);
    void onActUDS(bool checked);
    void onActTerminalUtf8();
    void onActTerminalGbk();
    void doIapUpgrade();

    MainWindow *main = nullptr;

    Ui::MainWindow *ui = nullptr;
    StatusBar       *m_status = nullptr;      // 自定义状态栏
    ChartManager    *chart_manager = nullptr; // 图表管理器
    ChartDialog     *m_dialog = nullptr;      // 图表控制对话框
    CanfdWidget     *canfd_widget = nullptr;
    UdsWidget       *uds_widget = nullptr;
    SerialWidget    *serial_widget = nullptr; // 串口调试控制面板
    ConsoleWidget   *console_widget = nullptr;// 串口控制台
    PidWidget       *pid_widget = nullptr;    // PID 调试面板
    CanfdManager    *canfd_manager = nullptr; // CAN-FD 管理器
    ModbusMaster    *modbus_master = nullptr; // Modbus 主站事务器（数据线程）
    SerialManager   *serial_manager = nullptr;// 串口管理器
    RecordManager   *record_manager = nullptr;// 记录管理器
    qint64           m_canfdDropLastShowMs = 0;   // CANFD 丢帧显示节流时刻
    LogAnalysis     *log_analysis = nullptr;  // 日志分析器
    QThread         *data_thread = nullptr;   // 数据中转站线程
    DataHub         *data_hub = nullptr;      // 数据中转站
};

MainWindow::Private::~Private()
{
    // 先停数据线程，再释放工作线程对象（无父）与 UI
    if (data_thread) {
        data_thread->quit();
        data_thread->wait();
    }
    delete data_hub;
    delete modbus_master;
    delete ui;
}

// ============================================================
// 组件组装
// ============================================================
void MainWindow::Private::build()
{
    ui = new Ui::MainWindow;
    ui->setupUi(main);

    // 首先创建一个状态栏
    m_status = new StatusBar;
    ui->statusbar->addPermanentWidget(m_status, 1);       // 添加状态栏

    buildRecord();                      // 构造日志和数据库
    buildChart();                       // 先构造图表
    buildUI_SerialPort();               // 再构造串口
    buildUI_SerialWidgets();            // 串口调试面板 + 控制台（独立组件）
    buildUI_Canfd();                    // CAN-FD 界面配置
    build_DataHub();                    // 最后创建 数据中转站
    buildUI_Others();
}

void MainWindow::Private::buildUI_SerialPort()
{
    ui->deviceConfig->setCurrentIndex(0);     // 默认显示串口配置界面
    serial_manager = new SerialManager(main);
    // 绑定串口管理器：高频数据中转走 data_hub 路径
    connect(serial_manager, &SerialManager::serialOpened, main,
            [this](bool success, const QString &msg) { doSerialOpened(success, msg); });
    connect(serial_manager, &SerialManager::serialClose, main, [this]() { doSerialClose(); });
    connect(serial_manager, &SerialManager::serialPortNumChanged, main,
            [this](const QStringList &portNum) { doSerialPortNumChanged(portNum); });
    // 启动串口管理器
    serial_manager->start();
}

void MainWindow::Private::buildUI_SerialWidgets()     // 串口调试面板 + 控制台（独立组件，直连 SerialManager）
{
    serial_widget  = new SerialWidget;
    console_widget = new ConsoleWidget;

    serial_widget->connectManager(serial_manager);
    serial_widget->setConsole(console_widget);
    console_widget->connectManager(serial_manager);

    // 控制台发送按钮 → 面板直发（含 Modbus 检查与周期发送）
    connect(console_widget, &ConsoleWidget::sendRequested,
            serial_widget, &SerialWidget::sendText);

    // 状态栏提示直连（不中转）
    serial_widget->setStatusBar(m_status);    // 面板状态提示直连状态栏
    console_widget->setStatusBar(m_status);   // 控制台状态提示直连状态栏

    // IAP 升级入口：跳转 UDS 页面 + 协议预选 LTM
    connect(serial_widget, &SerialWidget::iapUpgradeRequested, main, [this]() { doIapUpgrade(); });
    // 布局：控制面板插入 toolBox，控制台插入 splitter
    ui->toolBox->insertItem(1, serial_widget, "串口调试");
    ui->splitter->insertWidget(1, console_widget);
}

void MainWindow::Private::buildUI_Canfd()
{
    canfd_manager = new CanfdManager(main);
    canfd_widget  = new CanfdWidget;
    canfd_widget->connectManager(canfd_manager);            // 绑定 CAN-FD Manager

    uds_widget = new UdsWidget;
    uds_widget->connectManager(canfd_manager);              // 绑定 CAN-FD Manager（发送用）
    uds_widget->connectSerialManager(serial_manager);       // 绑定串口 Manager（LTM 协议通道）
    uds_widget->setRecordManager(record_manager);           // 升级日志写入记录模块

    // 绑定 CAN-FD 管理器：高频数据中转走 data_hub 路径
    connect(canfd_manager, &CanfdManager::canfdOpened, main,
            [this](bool success, const QString &msg) { doCanfdOpened(success, msg); });
    connect(canfd_manager, &CanfdManager::canfdClosed, main, [this]() { doCanfdClosed(); });
    connect(canfd_manager, &CanfdManager::canfdError, main,
            [this](const QString &msg) { doCanfdError(msg); });
    connect(canfd_manager, &CanfdManager::canfdOnlineChanged, main,
            [this](bool online) { doCanfdOnlineChanged(online); });
    connect(canfd_manager, &CanfdManager::canfdBusError, main,
            [this](uint32_t errCode, int channel) { doCanfdBusError(errCode, channel); });
    canfd_manager->start();

    // CAN-FD / UDS 升级界面直接进主区域 StackedWidget（widget 自带布局，无需页面壳）
    ui->stackedWidget->addWidget(canfd_widget);
    ui->actShowCanfd->setCheckable(true);

    // UDS 升级界面独立页（actUDS 双击切换，参考 CAN-FD 页面逻辑）
    ui->stackedWidget->addWidget(uds_widget);
    ui->actUDS->setCheckable(true);
    ui->stackedWidget->setCurrentIndex(0);      // 默认图表页

    // CAN-FD 设备列表（设备索引）
    ui->comboCanfd->clear();
    ui->comboCanfd->addItem(QString("USBCANFD_200U"));
    ui->comboCanfd->addItem(QString("USBCAN_200U"));
    ui->comboCanfd->addItem(QString("USBCANFD_100U"));
    setLabelColor(ui->labOperCanfd, "gray");    // 初始状态灯
}

void MainWindow::Private::buildRecord()                  // 日志与数据库创建
{
    record_manager = new RecordManager(main);
    log_analysis   = new LogAnalysis(main);
    log_analysis->connectManager(record_manager);          // 绑定记录管理器
    log_analysis->close();                                 // 先不要显示
    record_manager->start();                               // 启动记录管理器
}

void MainWindow::Private::buildChart()
{   // 新的图表模块：管理器 + 四类视图
    chart_manager = new ChartManager(MAX_CHANNEL_SIZE, main);   // 32 通道：串口 CH0-4 + CANFD 映射槽

    // 视图0 波形：直接放入 .ui 的 pageView
    const int viewWave = chart_manager->createView(View_Waveform);
    QVBoxLayout *waveLayout = new QVBoxLayout(ui->pageView);
    waveLayout->setContentsMargins(0, 0, 0, 0);
    waveLayout->addWidget(chart_manager->getViewWidget(viewWave));
    for (int ch = 0; ch < MAX_CHANNEL_SIZE; ++ch)
        chart_manager->attachChannel(viewWave, ch);             // 波形挂全部通道

    // 视图1~3 频谱 / 波形+频谱 / XY：动态创建页面，跟在 pageView 后面
    const ViewType types[3] = {View_Spectrum, View_WaveformSpectrum, View_XY};
    for (int i = 0; i < 3; ++i) {
        const int view = chart_manager->createView(types[i]);
        QWidget *page = new QWidget(ui->stackedWidget);
        ui->stackedWidget->addWidget(page);
        QVBoxLayout *lay = new QVBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(chart_manager->getViewWidget(view));
    }
    chart_manager->attachChannel(1, 0);                         // 频谱：CH1
    chart_manager->attachChannel(2, 0);                         // 波形+频谱：CH1
    chart_manager->attachChannel(3, 0);                         // XY：A=CH1
    chart_manager->attachChannel(3, 1);                         // XY：B=CH2

    m_dialog = new ChartDialog(main);                       // 图表控制对话框
    m_dialog->connectManager(chart_manager, MAX_CHANNEL_SIZE);
    connect(m_dialog, &ChartDialog::viewChanged, main,
            [this](int v) { ui->stackedWidget->setCurrentIndex(v); });   // 0~3 与四类视图页一一对应
    connect(chart_manager, &ChartManager::exportFinished, main,
            [this](bool success, const QString &msg) { doExportFinished(success, msg); });
    chart_manager->setPeriod(100);                      // 设置图表刷新周期：100ms
    chart_manager->start();
}

void MainWindow::Private::build_DataHub()             // 创建数据中转站
{
    data_hub    = new DataHub;
    data_thread = new QThread(main);
    data_hub->moveToThread(data_thread);     // 中转站对象移动到数据线程
    data_hub->setManager(chart_manager);     // 设置图表管理器
    data_hub->setSendChannels(serial_manager, canfd_manager);   // 发送路由注入：串口优先，否则 CAN-FD 0x100

    // 负责高频信号中转，极大减轻 UI主线程数据处理压力，避免界面卡顿。
    // 串口管理器的高频信号中转
    connect(serial_manager, &SerialManager::serialDataUpdated, data_hub, &DataHub::do_serialDataUpdated);
    connect(data_hub,       &DataHub::channelActualChanged, m_dialog, &ChartDialog::do_channelValues);   // 中转站 -> 图表控制器实际值
    connect(data_hub,       &DataHub::textOrCMDReceived,    main,
            [this](uint8_t type, const QString &str, const QByteArray &data) {
                doTextOrCMDReceived(type, str, data);
            });    // CAN-FD 管理器的高频信号中转
    connect(canfd_manager, &CanfdManager::canfdDataUpdated,    data_hub, &DataHub::do_canfdDataUpdated);
    connect(canfd_manager, &CanfdManager::framesSent,          data_hub, &DataHub::do_canfdFramesSent);
    connect(uds_widget,    &UdsWidget::upgradeActiveChanged,   data_hub, &DataHub::setUpgradeMode);  // 升级时旁路表格显示/图表，仅 UDS 收帧
    connect(data_hub,  &DataHub::canfdRawReceived,  uds_widget,   &UdsWidget::onFrameReceived);   // UDS 升级：原始帧经中转站转发（工作线程收，UI 零解析开销）
    connect(data_hub,  &DataHub::canfdRowsReceived, canfd_widget, &CanfdWidget::onFrameReceived);
    connect(data_hub,  &DataHub::canfdRowsSent,     canfd_widget, &CanfdWidget::onFramesSent);
    connect(data_hub,  &DataHub::canfdDropped,      main,
            [this](int dropped, uint64_t total) { doCanfdDropped(dropped, total); });    // 发送路由：面板控件通过中转站统一发送（串口优先，否则 CAN-FD 0x100）
    serial_widget->connectHub(data_hub);            // pid_widget 在 buildUI_Others 创建后再注入

    // Modbus 主站事务器：数据线程运行，发送通过回调注入串口
    modbus_master = new ModbusMaster;
    modbus_master->moveToThread(data_thread);
    modbus_master->setSendCallback([this](const QByteArray &bytes) {
        serial_manager->send(bytes);
    });
    data_hub->setModbusMaster(modbus_master);

    // 启动数据处理线程
    data_thread->start();
}

void MainWindow::Private::buildUI_Others()          // 其余部分的UI处理
{
    setLabelColor(ui->labOperate, "gray");

    // PID 调试面板（独立组件）：直连串口/状态栏；实际值由 DataHub 经 setActualSink 强写
    pid_widget = new PidWidget;
    pid_widget->connectManager(serial_manager);
    pid_widget->connectHub(data_hub);               // 发送路由：串口优先，否则 CAN-FD 0x100
    pid_widget->setStatusBar(m_status);             // 直连状态栏
    data_hub->setActualSink([this](int ch, float v) {
        pid_widget->setValue(ch, v);                // 强写实际值（仅写数据）
    });
    connect(data_hub, &DataHub::pidActualChanged, pid_widget, &PidWidget::refreshActual);  // 数据更新只刷实际值
    ui->toolBox->insertItem(0, pid_widget, "PID调试");
    ui->toolBox->setCurrentIndex(0);       // 默认显示 PID 调试界面

    // 控制台编码菜单：默认 UTF-8，互斥勾选（编码状态由 ConsoleWidget 持有）
    ui->actTerminalUtf8->setCheckable(true);
    ui->actTerminalGbk->setCheckable(true);
    QActionGroup *codecGroup = new QActionGroup(main);
    codecGroup->addAction(ui->actTerminalUtf8);
    codecGroup->addAction(ui->actTerminalGbk);
    ui->actTerminalUtf8->setChecked(true);
}

// ============================================================
// 槽实现
// ============================================================
void MainWindow::Private::onBtnStart()                    // 下位机启停按钮
{
    const QString str = ui->btnStart->text();
    if (str == "启动")
        data_hub->sendLtm(Data_CMD_Start, QByteArray());  // 统一发送：串口优先，否则 CAN-FD
    else
        data_hub->sendLtm(Data_CMD_Stop,  QByteArray());
}

void MainWindow::Private::doTextOrCMDReceived(uint8_t type, const QString &str, const QByteArray &data)
{
    switch (type)
    {
        case Data_CMD_Text:                         // 文本：统一收发后转控制台（少一次跨线程信号）
            if (console_widget) console_widget->appendReceived(data);
            break;
        case Data_CMD_Start:                        // 下位机响应启动
            ui->btnStart->setText("暂停");
            m_status->setInfo(str);
            break;
        case Data_CMD_Stop:                         // 下位机响应停止
            ui->btnStart->setText("启动");
            m_status->setInfo(str);
            break;
        default: break;
    }
}

void MainWindow::Private::doSerialOpened(bool success, const QString &msg)
{
    if (success) {
        setLabelColor(ui->labOperate, "green");
        ui->btnSerial->setText("关闭串口");
        m_status->setInfo(msg);
        QMessageBox::information(main, "信息", msg);
        LOG_INFO("打开串口成功");
        data_hub->setSerialOnline(true);     // 发送路由：串口在线
    } else {
        m_status->setInfo(msg);
        QMessageBox::critical(main, "错误", msg);
        LOG_ERROR("打开串口失败");
    }
}

void MainWindow::Private::doSerialClose()
{
    const QString msg = "关闭串口成功";
    QMessageBox::information(main, "信息", msg);
    m_status->setInfo(msg);
    ui->btnSerial->setText("打开串口");
    setLabelColor(ui->labOperate, "gray");                  // 切换状态指示灯
    data_hub->setSerialOnline(false);       // 发送路由：串口离线
}

void MainWindow::Private::doSerialPortNumChanged(const QStringList &portNum)
{
    ui->comboPort->clear();                                 // 清空原有的端口数据
    ui->comboPort->addItems(portNum);                       // 更新数据
}

void MainWindow::Private::onBtnCanfd()
{
    if (ui->btnCanfd->text() == "打开CAN-FD") {
        CanfdConfig config;
        config.deviceIndex   = (uint32_t)ui->comboCanfd->currentIndex();
        config.abitBaud      = parseCanfdBaud(ui->comboCanBaud->currentText());
        config.dbitBaud      = parseCanfdBaud(ui->comboCanfdBaud->currentText());
        config.canfdStandard = ui->comboStandard->currentIndex();
        config.channels      = 2;
        canfd_manager->open(config);
    } else {
        canfd_manager->close();
    }
}

void MainWindow::Private::doCanfdOpened(bool success, const QString &msg)
{
    m_status->setInfo(msg);
    if (success) {
        setLabelColor(ui->labOperCanfd, "green");
        ui->btnCanfd->setText("关闭CAN-FD");
        QMessageBox::information(main, "信息", msg);
        LOG_INFO("CAN-FD 打开成功");
    } else {
        setLabelColor(ui->labOperCanfd, "red");
        ui->btnCanfd->setText("打开CAN-FD");
        QMessageBox::critical(main, "错误", msg);
        LOG_ERROR("CAN-FD 打开失败");
    }
}

void MainWindow::Private::doCanfdClosed()
{
    const QString msg = "关闭 CAN-FD 成功";
    QMessageBox::information(main, "信息", msg);
    setLabelColor(ui->labOperCanfd, "gray");
    ui->btnCanfd->setText("打开CAN-FD");
    m_status->setInfo(msg);
}

void MainWindow::Private::doCanfdError(const QString &msg)
{
    LOG_ERROR(QString("CANFD ") + msg);
    m_status->setInfo(msg);
}

void MainWindow::Private::doCanfdOnlineChanged(bool online)
{
    QString str = "[CANFD] 设备恢复在线";
    if (online) {
        console_widget->appendText(str + "\n");
        LOG_INFO(str);
    } else {
        str = "[CANFD] 设备掉线!";
        QMessageBox::warning(main, QString("CAN-FD"), str);
        console_widget->appendText(str + "\n");
        LOG_ERROR(str);
    }
    m_status->setInfo(str);
}

void MainWindow::Private::doCanfdBusError(uint32_t errCode, int channel)
{
    QString str = QString("[CANFD] 通道%1 总线错误 0x%2\n").arg(channel + 1).arg(errCode, 0, 16);
    console_widget->appendText(str);
    LOG_WARN(str);
}

void MainWindow::Private::doCanfdDropped(int dropped, uint64_t total)
{
    // 高频接收时丢帧是运行常态，不写日志文件；限频显示（3s 一次）避免刷屏
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_canfdDropLastShowMs < 3000)
        return;
    m_canfdDropLastShowMs = now;
    const QString str = QString("[CANFD] 接收缓冲不足，已丢弃 %1 帧（累计 %2）")
                            .arg(dropped).arg(total);
    m_status->setInfo(str);
    console_widget->appendText(str + "\n");
}

void MainWindow::Private::doExportFinished(bool success, const QString &msg)
{
    m_status->setInfo(msg);
    if (success)
        QMessageBox::information(main, "信息", msg);
    else
        QMessageBox::warning(main, "警告", msg);
}

void MainWindow::Private::onBtnSerial()
{
    if (ui->btnSerial->text() == "打开串口") {
        static SerialConfig config;                                     // 配置串口信息
        config.port = ui->comboPort->currentIndex();                    // 端口号
        config.stop = (uint8_t)(ui->comboStop->currentText().toInt());  // 停止位
        config.data = (uint8_t)(ui->comboData->currentText().toInt());  // 数据位
        config.baud = (uint32_t)(ui->comboBaud->currentText().toInt()); // 波特率
        const int checkIdx = ui->comboCheck->currentIndex();            // 校验位
        config.check = (checkIdx == 0) ? 0 : (checkIdx == 1) ? 3 : 2;
        serial_manager->open(config);                                   // 打开串口
    } else {
        serial_manager->close();                                        // 关闭串口
    }
}

void MainWindow::Private::onBtnDataExport()
{
    const QString fileName = QFileDialog::getSaveFileName(main, "导出CSV数据", "", "CSV文件(*.csv)");
    if (fileName.isEmpty()) return;
    // 临时控件：选择导出时长（1/2/5/10/20/30 分钟，或全部数据）
    const QStringList items = {"1分钟", "2分钟", "5分钟", "10分钟", "20分钟", "30分钟", "全部数据"};
    const double seconds[]  = {60.0, 120.0, 300.0, 600.0, 1200, 1800.0, -1.0};
    bool ok = false;
    const QString sel = QInputDialog::getItem(main, "导出数据", "请选择导出数据时长",
                                              items, items.size() - 1, false, &ok);
    if (!ok) return;

    chart_manager->exportData(fileName, seconds[items.indexOf(sel)]);
}

void MainWindow::Private::onBtnChartShow()
{
    ui->stackedWidget->setCurrentIndex(0);         // 显示图表
    m_dialog->show();
    m_status->setInfo("打开图表控制器");
}

void MainWindow::Private::onBtnMode()
{
    m_status->setInfo("新版图表支持内置拖拽/缩放，无需切换模式");
}

void MainWindow::Private::onBtnReset()      // 复位操作
{
    data_hub->sendLtm(Data_CMD_Reset, QByteArray());
    m_status->setInfo("发送复位命令");
    chart_manager->clearShow();             // 界面清空
}

void MainWindow::Private::onBtnClearShow()  // 清空显示
{
    chart_manager->clearShow();
    m_status->setInfo("清空显示");
}

void MainWindow::Private::onBtnStopShow()   // 停止显示
{
    chart_manager->stopShow();
    m_status->setInfo("停止显示曲线");
}

void MainWindow::Private::onActImportDB()
{
    // SQLite 数据库导入（预留）
}

void MainWindow::Private::onActOpenLog()                 // 打开日志分析器
{
    log_analysis->show();
    m_status->setInfo("打开日志分析器");
}

void MainWindow::Private::onActUseIntro()
{
    console_widget->showHelp();     // 与帮助按键的输出一致（控制台组件）
}

void MainWindow::Private::onActShowCanfd(bool checked)
{
    ui->stackedWidget->setCurrentWidget(checked ? canfd_widget : ui->pageView);
}

void MainWindow::Private::onActUDS(bool checked)
{
    if (checked) {
        uds_widget->setProtocol(0);         // UDS 升级默认走 CAN-FD
        ui->stackedWidget->setCurrentWidget(uds_widget);
    } else {
        ui->stackedWidget->setCurrentWidget(ui->pageView);
    }
}

void MainWindow::Private::onActTerminalUtf8()
{
    console_widget->setCodec(0);      // 控制台编码：UTF-8
    m_status->setInfo("控制台编码：UTF-8");
}

void MainWindow::Private::onActTerminalGbk()
{
    console_widget->setCodec(1);      // 控制台编码：GBK
    m_status->setInfo("控制台编码：GBK");
}

void MainWindow::Private::doIapUpgrade()                            // IAP升级：跳转 UDS 页面，协议预选 LTM
{
    // 直接切页 + 预选协议，不依赖 actUDS 信号链
    uds_widget->setProtocol(1);                             // 协议显示：LTM 协议
    ui->stackedWidget->setCurrentWidget(uds_widget);
    const QSignalBlocker blocker(ui->actUDS);               // 同步菜单勾选，不触发 triggered 信号
    ui->actUDS->setChecked(true);
    m_status->setInfo(ui->btnSerial->text() == "关闭串口"
                          ? "IAP 升级：请选择固件并开始升级（LTM 协议）"
                          : "IAP 升级：串口未打开，请先打开串口再升级");
}

// ============================================================
// 公共接口：构造/析构 + 槽委托
// ============================================================
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), pimpl(new Private(this)) { pimpl->build(); }
MainWindow::~MainWindow()                   { delete pimpl; }
void MainWindow::on_btnSerial_clicked()     { pimpl->onBtnSerial(); }
void MainWindow::on_btnCanfd_clicked()      { pimpl->onBtnCanfd(); }
void MainWindow::on_btnDataExport_clicked() { pimpl->onBtnDataExport(); }
void MainWindow::on_btnChartShow_clicked()  { pimpl->onBtnChartShow(); }
void MainWindow::on_btnStart_clicked()      { pimpl->onBtnStart(); }
void MainWindow::on_btnMode_clicked()       { pimpl->onBtnMode(); }
void MainWindow::on_btnReset_clicked()      { pimpl->onBtnReset(); }
void MainWindow::on_btnStopShow_clicked()   { pimpl->onBtnStopShow(); }
void MainWindow::on_btnClearShow_clicked()  { pimpl->onBtnClearShow(); }

void MainWindow::on_actImportDB_triggered() { pimpl->onActImportDB(); }
void MainWindow::on_actOpenLog_triggered()  { pimpl->onActOpenLog(); }
void MainWindow::on_actUseIntro_triggered() { pimpl->onActUseIntro(); }
void MainWindow::on_actShowCanfd_triggered(bool checked) { pimpl->onActShowCanfd(checked); }
void MainWindow::on_actUDS_triggered(bool checked)       { pimpl->onActUDS(checked); }
void MainWindow::on_actTerminalUtf8_triggered()          { pimpl->onActTerminalUtf8(); }
void MainWindow::on_actTerminalGbk_triggered()           { pimpl->onActTerminalGbk(); }

// ============================================================
// 内部函数
// ============================================================
static void setLabelColor(QLabel *label, const QString &color)
{
    label->setText(QString("操作<span style='font-size:16px; color:%1;'>●</span>").arg(color));
}

// "1Mbps"/"800kbps" -> 波特率数值
static uint32_t parseCanfdBaud(const QString &s)
{
    if (s.endsWith(QString("Mbps")))
        return (uint32_t)s.left(s.size() - 4).toUInt() * 1000000;
    if (s.endsWith(QString("kbps")))
        return (uint32_t)s.left(s.size() - 4).toUInt() * 1000;
    return (uint32_t)s.toUInt();
}
