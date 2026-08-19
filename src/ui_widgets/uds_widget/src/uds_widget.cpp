#include "uds_widget.h"
#include "ui_uds_widget.h"
#include "canfd_manager.h"
#include "serial.h"
#include "record.h"
#include "uds_server.h"
#include "ltm_protocol.h"

#include <cstring>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>

// ============================================================
// 传输层常量（CAN-FD / ISO-TP SF_CanFD）
// ============================================================
static constexpr int UDS_FAST_POLL_MS = 3;        // 升级期间轮询周期（加速响应读取）
static constexpr int IAP_RESET_DELAY_MS = 1000;   // IAP：发复位指令后等 BootLoader 就绪再启动升级

// ============================================================
// 私有实现（Pimpl）：UI / 协议引擎 / 传输层全部收敛于此
// ============================================================
class UdsWidget::Private
{
public:
    explicit Private(UdsWidget *uds) : uds(uds) {}

    void setup();                       // UI 创建 + 协议引擎接线
    void connectSerial(SerialManager *manager);     // LTM 协议通道绑定
    void setProtocol(int index);
    void onSelect();                    // 选择固件
    void onStart();                     // 开始升级
    void onStop();                      // 停止升级
    void onClear();                     // 清除升级日志输出
    void onFrameReceived(const QList<CanfdFrame> &frames);
    void log(const QString &msg);       // 日志输出（时间戳 + 界面 + 记录模块）
    bool sendUds(const QByteArray &payload);        // 打包 SF_CanFD 单帧并发送
    void finish(bool ok, const QString &msg);       // 升级结束：恢复轮询/按钮/中转站

    bool parseHexId(const QString &s, quint32 &id); // 解析 "0x7F0"
    bool parseHexAddr(const QString &s, quint32 &addr);

    UdsWidget      *uds = nullptr;
    Ui::UdsWidget  *ui = nullptr;
    CanfdManager   *m_manager = nullptr;    // CAN-FD 管理器（发送用）
    SerialManager  *m_serial  = nullptr;    // LTM 协议通道（串口）
    LtmProtocol     m_ltm;               // LTM 帧解析（IAP 响应重组）
    RecordManager  *m_record  = nullptr;    // 日志记录模块
    UdsServer      *m_server  = nullptr;    // 协议引擎组件（硬件无关）

    quint32        m_reqId    = 0x7F0;
    quint32        m_respId   = 0x7F1;
    QByteArray     m_firmware;
    int            m_normalPollMs = 20;     // 正常轮询周期（升级完成后恢复）
};

void UdsWidget::Private::setup()
{
    ui = new Ui::UdsWidget;
    ui->setupUi(uds);

    ui->btnStop->setEnabled(false);

    // 协议引擎信号 → 界面/传输层（lambda 以 uds 为接收上下文，随控件销毁自动断开）
    m_server = new UdsServer(uds);
    connect(m_server, &UdsServer::sendRequest, uds,
            [this](const QByteArray &payload) { sendUds(payload); });
    connect(m_server, &UdsServer::progressChanged, uds,
            [this](int percent, const QString &text) {
                ui->progress->setValue(percent);
                ui->labelState->setText(text);
            });
    connect(m_server, &UdsServer::logMessage, uds,
            [this](const QString &msg) { log(msg); });
    connect(m_server, &UdsServer::finished, uds,
            [this](bool ok, const QString &msg) { finish(ok, msg); });
}

void UdsWidget::Private::connectSerial(SerialManager *manager)
{
    m_serial = manager;
    if (!m_serial) return;
    // LTM 协议通道：请求走 Data_User_Defined 帧，响应字节经 LtmProtocol 重组解析
    connect(m_serial, &SerialManager::serialDataUpdated, uds,
            [this](const QByteArray &bytes) {
                m_ltm.receive(bytes);
                uint8_t type;
                QByteArray data;
                while (m_ltm.process(type, data)) {
                    if (type == Data_User_Defined)
                        m_server->onResponse(data);
                }
            });
}

void UdsWidget::Private::setProtocol(int index)
{
    ui->comboProt->setCurrentIndex(index);      // 0=CAN-FD / 1=LTM 协议
}

void UdsWidget::Private::onSelect()
{
    const QString file = QFileDialog::getOpenFileName(uds, "选择固件", QString(),
                                                      "固件文件 (*.bin);;所有文件 (*)");
    if (file.isEmpty()) return;
    ui->editFile->setText(file);
    log("选择固件: " + file);
}

void UdsWidget::Private::onStart()
{
    // 按所选协议检查传输通道：0=CAN-FD / 1=LTM 协议
    const int protocol = ui->comboProt->currentIndex();
    if (protocol == 1) {
        if (!m_serial) {
            QMessageBox::warning(uds, "UDS 升级", "串口通道未绑定");
            return;
        }
    } else {
        if (!m_manager || !m_manager->isActive()) {
            QMessageBox::warning(uds, "UDS 升级", "CAN-FD 设备未打开");
            return;
        }
    }

    // 读取配置：请求/响应 ID、App 起始地址 → 传入协议引擎
    quint32 reqId = 0, respId = 0, appBase = 0;
    if (!parseHexId(ui->editReqId->text(), reqId)   ||
        !parseHexId(ui->editRespId->text(), respId) ||
        !parseHexAddr(ui->editAppBase->text(), appBase)) {
        QMessageBox::warning(uds, "UDS 升级", "ID / 地址格式错误，应为 0x 开头十六进制");
        return;
    }
    m_reqId  = reqId;
    m_respId = respId;
    m_server->setAppBase(appBase);

    QFile file(ui->editFile->text());
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(uds, "UDS 升级", "请先选择固件文件");
        return;
    }
    m_firmware = file.readAll();
    file.close();

    if (m_firmware.isEmpty()) {
        QMessageBox::warning(uds, "UDS 升级", "固件文件为空");
        return;
    }
    log(QString("开始升级：固件 %1 字节，App 起始 0x%2")
            .arg(m_firmware.size())
            .arg(QString::number(appBase, 16).rightJustified(8, '0')));

    m_server->setChunkSize(protocol == 1 ? 100 : 60);   // LTM 载荷上限 128-SID-序号 / CAN-FD 单帧 62
    if (protocol == 0) {
        // 升级加速：CAN-FD 轮询周期临时压到 3ms，升级结束恢复（finish 中处理）
        m_manager->setPollInterval(UDS_FAST_POLL_MS);
    }
    emit uds->upgradeActiveChanged(true);       // 通知中转站旁路显示，只转发原始帧给 UDS

    ui->btnStart->setEnabled(false);
    ui->btnStop->setEnabled(true);
    ui->progress->setValue(0);

    if (protocol == 1) {
        // IAP：先发复位指令，App 软复位到 BootLoader；等其 UART 就绪再启动 UDS 升级。
        // App 未运行 / BootLoader 已驻留时，复位帧被忽略，无副作用。
        m_serial->send(m_ltm.package(Data_CMD_Reset, QByteArray()));
        QTimer::singleShot(IAP_RESET_DELAY_MS, uds,
                           [this]() { m_server->start(m_firmware); });
    } else {
        m_server->start(m_firmware);
    }
}

void UdsWidget::Private::onStop()
{
    m_server->stop();
}

void UdsWidget::Private::onClear()
{
    ui->logView->clear();
}

bool UdsWidget::Private::sendUds(const QByteArray &payload)
{
    // LTM 协议通道：UDS PDU 包进 Data_User_Defined 帧（帧打包由串口协议层完成）
    if (ui->comboProt->currentIndex() == 1) {
        if (!m_serial) return false;
        m_serial->send(m_ltm.package(Data_User_Defined, payload));
        return true;
    }

    if (!m_manager) return false;

    CanfdFrame frame;
    frame.id    = m_reqId;                                // 请求 ID（标准帧）
    frame.isFd  = true;
    frame.flags = 0x01;                                   // BRS 加速

    // SF_CanFD 帧长度补全到 DLC 槽大小（8/12/16/20/24/32/48/64）：
    // 非 DLC 字节数（如 13）发送时尾部会被固件清零，与手动发送接口的补 0 行为一致
    int frameLen = payload.size() + 2;
    if (frameLen > 8) {
        static const uint8_t s_dlcLen[16] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
        uint8_t dlc = (uint8_t)(8 + ((frameLen - 8 + 3) >> 2));
        if (dlc > 15) dlc = 15;
        frameLen = s_dlcLen[dlc];                       /* 13 → 16，62 → 64 */
    }
    frame.len = uint8_t(frameLen);
    frame.data.resize(frameLen);                        /* 后部自动补 0 */
    frame.data[0] = char(0x00);
    frame.data[1] = char(payload.size());
    memcpy(frame.data.data() + 2, payload.constData(), payload.size());
    m_manager->send(frame);

    return true;
}

void UdsWidget::Private::onFrameReceived(const QList<CanfdFrame> &frames)
{
    if (!m_server->isActive()) return;

    for (const CanfdFrame &frame : frames) {
        if (frame.rawId() != m_respId) continue;         // 只处理本机响应 ID

        // 解析 SF_CanFD：PCI=0x00，长度在 data[1]
        if (frame.data.size() < 2 || uint8_t(frame.data[0]) != 0x00)
            continue;
        const int len = uint8_t(frame.data[1]);
        if (len + 2 > frame.data.size()) continue;

        m_server->onResponse(frame.data.mid(2, len));
        if (!m_server->isActive()) break;                // 升级已结束（成功/失败），不再处理后续帧
    }
}

void UdsWidget::Private::finish(bool ok, const QString &msg)
{
    /* CAN-FD 通道恢复正常轮询周期 */
    if (ui->comboProt->currentIndex() == 0 && m_manager)
        m_manager->setPollInterval(m_normalPollMs);

    emit uds->upgradeActiveChanged(false);      /* 恢复中转站正常显示 */

    ui->btnStart->setEnabled(true);
    ui->btnStop->setEnabled(false);
    ui->progress->setValue(ok ? 100 : ui->progress->value());
    ui->labelState->setText(ok ? "完成" : "失败");

    if (m_record) {
        const QString line = QString("%1 %2").arg(ok ? "[OK]" : "[FAIL]").arg(msg);
        if (ok) m_record->logInfo(line);
        else    m_record->logError(line);
    }
}

// ============================================================
// 工具
// ============================================================
bool UdsWidget::Private::parseHexId(const QString &s, quint32 &id)
{
    bool ok = false;
    const QString t = s.trimmed();
    const quint32 v = t.toUInt(&ok, 0);                 // 0x 前缀自动识别
    if (!ok || v > 0x7FF) return false;
    id = v;
    return true;
}

bool UdsWidget::Private::parseHexAddr(const QString &s, quint32 &addr)
{
    bool ok = false;
    const QString t = s.trimmed();
    const quint32 v = t.toUInt(&ok, 0);
    if (!ok || v == 0) return false;
    addr = v;
    return true;
}

void UdsWidget::Private::log(const QString &msg)
{
    const QString line = QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + msg;
    ui->logView->appendPlainText(line);
    if (m_record) m_record->logInfo(msg);       // 升级日志同时写入记录模块
}

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
UdsWidget::UdsWidget(QWidget *parent)
    : QWidget(parent)
    , pimpl(new Private(this))
{
    pimpl->setup();
}

UdsWidget::~UdsWidget()
{
    delete pimpl;
}

void UdsWidget::connectManager(CanfdManager *manager)
{
    pimpl->m_manager = manager;     /* 仅用于发送；接收由中转站 canfdRawReceived 转发 */
}

void UdsWidget::connectSerialManager(SerialManager *manager)
{
    pimpl->connectSerial(manager);
}

void UdsWidget::setRecordManager(RecordManager *manager)
{
    pimpl->m_record = manager;
}

void UdsWidget::setProtocol(int index)
{
    pimpl->setProtocol(index);
}

void UdsWidget::onFrameReceived(const QList<CanfdFrame> &frames)
{
    pimpl->onFrameReceived(frames);
}

void UdsWidget::on_btnSelect_clicked() { pimpl->onSelect(); }
void UdsWidget::on_btnStart_clicked()  { pimpl->onStart(); }
void UdsWidget::on_btnStop_clicked()   { pimpl->onStop(); }
void UdsWidget::on_btnClear_clicked()  { pimpl->onClear(); }
