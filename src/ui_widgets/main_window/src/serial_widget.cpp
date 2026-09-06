#include "serial_widget.h"
#include "ui_serial_widget.h"

#include "serial.h"
#include "ltm_protocol.h"
#include "data_hub.h"
#include "console_widget.h"
#include "status_bar.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

// ============================================================
// 私有实现（Pimpl）：UI 与全部状态收敛于此
// ============================================================
class SerialWidget::Private
{
public:
    explicit Private(SerialWidget *q) : q(q) {}

    void setup();                       // UI 创建
    void setProtocol(int index);
    void sendText(const QByteArray &data);
    void setInfo(const QString &msg);

    void onProtocolChanged(int index);
    void onPeriodToggled(int arg1);
    void onHexSendToggled(int arg1);
    void onNewLineToggled(int arg1);
    void onHexShowToggled(int arg1);
    void onPauseClicked();
    void onClearClicked();
    void onSendFile();
    void onIapUpgrade();

    SerialWidget  *q = nullptr;
    Ui::SerialWidget *ui = nullptr;
    SerialManager *m_manager = nullptr;
    DataHub       *m_hub = nullptr;      // 发送路由（可空）
    ConsoleWidget *m_console = nullptr;
    StatusBar     *m_bar     = nullptr;
    QByteArray     m_fileData;
};

void SerialWidget::Private::setup()
{
    ui = new Ui::SerialWidget;
    ui->setupUi(q);
}

void SerialWidget::Private::setProtocol(int index)
{
    ui->comboProt->setCurrentIndex(index);      // 触发 onProtocolChanged → setProtocol + 状态
}

void SerialWidget::Private::setInfo(const QString &msg)
{
    if (m_bar) m_bar->setInfo(msg);
}

// 供 ConsoleWidget 发送按钮调用：发送指令 + 周期发送
void SerialWidget::Private::sendText(const QByteArray &data)
{
    if (!m_manager) return;
    if (ui->comboProt->currentIndex() == Prot_Modbus) { // Modbus RTU 协议下不允许该接口
        setInfo("Modbus 模式下不支持 ‘发送指令’ 接口！");
        return;
    } 
    if (m_hub) m_hub->sendLtm(Data_CMD_Text, data);     // 该接口同时支持普通串口和LTM，内部自动判断
    if (ui->chkSendPeriod->isChecked() && m_hub)
        m_hub->startPeriodSendLtm(Data_CMD_Text, data, ui->spinSendPeriod->value());
}

// ============================================================
// 私有实现细节（控件逻辑）
// ============================================================
void SerialWidget::Private::onProtocolChanged(int index)
{
    if (!m_manager) return;
    QString str;
    if (m_hub) m_hub->setSerialProtocol(index);
    if (index == Prot_LTM)          str = "当前通讯协议 ==》LTM协议！";
    else if (index == Prot_Common)  str = "当前通讯协议 ==》普通串口！";
    else if (index == Prot_Modbus)  str = "当前通讯协议 ==》Modbus RTU！";
    setInfo(str);
}

void SerialWidget::Private::onPeriodToggled(int arg1)
{
    if (arg1 == Qt::Checked) {
        setInfo("周期发送已开启，点击发送后生效");   // 只标记模式，不启动
    } else {
        if (m_hub) m_hub->stopPeriodSendLtm();                  // 核心：立即停
        setInfo("取消周期命令发送！");
    }
}

void SerialWidget::Private::onHexSendToggled(int arg1)
{
    const bool on = (arg1 == Qt::Checked);
    if (m_console) m_console->setHexSend(on);                   // 编辑框文本 hex <-> 原文
    setInfo(on ? "发送十六进制数据！" : "发送原始数据！");
}

void SerialWidget::Private::onNewLineToggled(int arg1)
{
    if (m_console) m_console->setNewLine(arg1 == Qt::Checked);
    setInfo((arg1 == Qt::Checked)
                ? "指令输出自动添加回车换行符！"
                : "正常指令输出！");
}

void SerialWidget::Private::onHexShowToggled(int arg1)
{
    const bool on = (arg1 == Qt::Checked);
    if (m_console) m_console->setHexShow(on);                   // 控制台刷新显示
    setInfo(on ? "控制台接收以 16进制 显示！" : "控制台接收正常显示！");
}

void SerialWidget::Private::onPauseClicked()
{
    const bool pause = (ui->btnStopRecv->text() == "暂停接收");
    if (m_console) m_console->setPaused(pause);
    ui->btnStopRecv->setText(pause ? "继续接收" : "暂停接收");
    setInfo(pause ? "控制台已暂停接收！" : "控制台已恢复接收！");
}

void SerialWidget::Private::onClearClicked()
{
    if (m_console) m_console->clear();
}

void SerialWidget::Private::onSendFile()                 // 发送文件（普通串口协议）
{
    if (!ui->comboProt->currentIndex()) {                // 0: LTM 协议
        QMessageBox::warning(q, "协议不支持",
                             "LTM 协议不支持文件发送！\n请切换到「普通串口」协议后再试。");
        return;
    }

    const QString fileName = QFileDialog::getOpenFileName(
        q, "选择要发送的文件", QCoreApplication::applicationDirPath(),
        "所有文件 (*.*);;二进制文件 (*.bin);;Hex文件 (*.hex)");
    if (fileName.isEmpty()) { setInfo("打开文件已取消"); return; }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(q, "文件打开失败",
            QString("无法打开文件：%1\n错误信息：%2").arg(fileName).arg(file.errorString()));
        return;
    }
    m_fileData = file.readAll();
    file.close();

    if (m_fileData.isEmpty()) {
        QMessageBox::warning(q, "文件为空", "所选文件为空文件，无法发送！");
        setInfo("文件为空: " + QFileInfo(fileName).fileName());
        return;
    }

    setInfo(QString("已加载文件: %1 (%2 字节)")
                .arg(QFileInfo(fileName).fileName())
                .arg(m_fileData.size()));

    const QMessageBox::StandardButton reply = QMessageBox::question(
        q, "确认发送",
        QString("即将发送文件 (%1 字节) 到串口，是否继续？").arg(m_fileData.size()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) { setInfo("文件发送已取消"); return; }

    // 分片发送（适合大文件）
    const int PACKET_SIZE = 128;
    int sentBytes = 0;
    int packetIndex = 0;
    setInfo(QString("开始发送文件，总大小: %1 字节").arg(m_fileData.size()));

    while (sentBytes < m_fileData.size()) {
        const int chunkSize = qMin(PACKET_SIZE, m_fileData.size() - sentBytes);
        if (m_manager) m_manager->send(m_fileData.mid(sentBytes, chunkSize));   // 普通串口：原始字节
        sentBytes += chunkSize;
        packetIndex++;
        if (packetIndex % 100 == 0 || sentBytes >= m_fileData.size()) {
            const int progress = (sentBytes * 100) / m_fileData.size();
            setInfo(QString("发送进度: %1% (%2/%3 字节)")
                        .arg(progress).arg(sentBytes).arg(m_fileData.size()));
        }
    }
    setInfo(QString("文件发送完成！共发送 %1 字节，%2 包")
                .arg(sentBytes).arg(packetIndex));
}

void SerialWidget::Private::onIapUpgrade()
{
    emit q->iapUpgradeRequested();      // MainWindow 跳转 UDS 页面 + 协议预选 LTM
}

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
SerialWidget::SerialWidget(QWidget *parent)
    : QWidget(parent)
    , pimpl(new Private(this))
{
    pimpl->setup();
}

SerialWidget::~SerialWidget()
{
    delete pimpl;
}

void SerialWidget::connectManager(SerialManager *manager) { pimpl->m_manager = manager; }
void SerialWidget::connectHub(DataHub *hub)             { pimpl->m_hub = hub; }
void SerialWidget::setConsole(ConsoleWidget *console)     { pimpl->m_console = console; }
void SerialWidget::setStatusBar(StatusBar *bar)           { pimpl->m_bar = bar; }
void SerialWidget::setProtocol(int index)                 { pimpl->setProtocol(index); }
void SerialWidget::sendText(const QByteArray &data)       { pimpl->sendText(data); }

void SerialWidget::on_comboProt_currentIndexChanged(int index) { pimpl->onProtocolChanged(index); }
void SerialWidget::on_chkSendPeriod_stateChanged(int arg1)     { pimpl->onPeriodToggled(arg1); }
void SerialWidget::on_chkHexSend_stateChanged(int arg1)        { pimpl->onHexSendToggled(arg1); }
void SerialWidget::on_chkSendNewL_stateChanged(int arg1)       { pimpl->onNewLineToggled(arg1); }
void SerialWidget::on_chkHexShow_stateChanged(int arg1)        { pimpl->onHexShowToggled(arg1); }
void SerialWidget::on_btnStopRecv_clicked()                    { pimpl->onPauseClicked(); }
void SerialWidget::on_btnClearRecv_clicked()                   { pimpl->onClearClicked(); }
void SerialWidget::on_btnSendFile_clicked()                    { pimpl->onSendFile(); }
void SerialWidget::on_btnIapUpgrade_clicked()                  { pimpl->onIapUpgrade(); }
