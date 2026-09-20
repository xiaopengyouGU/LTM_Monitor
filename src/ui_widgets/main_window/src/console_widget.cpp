#include "console_widget.h"
#include "data_hub.h"
#include "ltm_protocol.h"
#include "ui_console_widget.h"

#include "serial.h"
#include "status_bar.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QTextStream>
#include <QStringConverter>
#include <memory>

// ============================================================
// 私有实现（Pimpl）：UI 与全部状态收敛于此
// ============================================================
class ConsoleWidget::Private
{
public:
    explicit Private(ConsoleWidget *q) : q(q) {}

    void setup();
    void setCodec(int codec);
    void showHelp();
    void appendText(const QString &text);
    void setHexSend(bool on);
    void setNewLine(bool on);
    void setHexShow(bool on);
    void setPaused(bool on);
    void clear();

    void onSend();
    void onClear();
    void onSave();
    void onHelp();

    void displayText(const QByteArray &data);
    QString toHexDisplay(const QString &rawText) const;
    QString fromHexDisplay(const QString &hexDisplay) const;
    QString decodeTerminal(const QByteArray &data) const;
    QByteArray buildSendData();
    void updateDisplay();
    void setInfo(const QString &msg);

    ConsoleWidget  *q = nullptr;
    Ui::ConsoleWidget *ui = nullptr;
    SerialManager *m_manager = nullptr;
    StatusBar     *m_bar     = nullptr;

    bool        m_recv       = true;    // 接收标志（暂停）
    bool        m_newLine    = true;    // 发送新行（面板控件直调）
    bool        m_hexSendMode = false;  // 16进制发送模式（面板控件直调）
    bool        m_hexShowMode = false;  // 16进制显示模式
    QByteArray  m_rawReceivedData;      // 保存所有接收到的原始数据（二进制）
    int         m_codec      = 0;       // 控制台编码：0=UTF-8，1=GBK
    std::unique_ptr<QStringDecoder> m_decoder;
    void resetDecoder()
    {
        m_decoder = std::make_unique<QStringDecoder>(
            m_codec == 1 ? QStringConverter::System : QStringConverter::Utf8);
    }
};

void ConsoleWidget::Private::setup()
{
    ui = new Ui::ConsoleWidget;
    ui->setupUi(q);
    resetDecoder();
}

void ConsoleWidget::Private::setCodec(int codec)
{
    m_codec = codec;
    resetDecoder();
}

void ConsoleWidget::Private::setInfo(const QString &msg)
{
    if (m_bar) m_bar->setInfo(msg);
}

// ============================================================
// 公共槽：由 SerialWidget 面板控件直调
// ============================================================
void ConsoleWidget::Private::setHexSend(bool on)
{
    if (on) {
        m_hexSendMode = true;
        ui->editCMD->setText(toHexDisplay(ui->editCMD->text()));     // 编辑框转 hex
    } else {
        m_hexSendMode = false;
        ui->editCMD->setText(fromHexDisplay(ui->editCMD->text().trimmed()));  // hex 还原
    }
}

void ConsoleWidget::Private::setNewLine(bool on)
{
    m_newLine = on;
}

void ConsoleWidget::Private::setHexShow(bool on)
{
    m_hexShowMode = on;
    resetDecoder();
    updateDisplay();                            // 刷新显示
}

void ConsoleWidget::Private::setPaused(bool on)
{
    m_recv = !on;
}

void ConsoleWidget::Private::clear()
{
    ui->plainTextEdit->clear();
    QFont font = ui->plainTextEdit->font();
    font.setPointSize(11);
    ui->plainTextEdit->setFont(font);
    m_rawReceivedData.clear();
    resetDecoder();
}

// ============================================================
// 帮助
// ============================================================
void ConsoleWidget::Private::showHelp()
{
    static const QStringList helpLines = {
        "LTM_Monitor上位机监控调试软件 ==> 帮助指令：",
        "1、配置好串口参数后,点击 '打开串口' 启动数据交互。",
        "2、'PID调试'页（默认）：设置 Kp/Ki/Kd、目标值等参数下发，实际值实时显示。",
        "3、'串口调试'页：切换协议（LTM/普通串口/Modbus）、定时/16进制发送、发送文件；'IAP升级'走串口 LTM 协议。",
        "4、点击 '打开CAN-FD' 启动 CAN/CAN-FD 模块；菜单可导入 DBC/自定义协议，配合映射表解码到图表通道。",
        "5、菜单 'UDS升级' 切换升级页：CAN-FD 通道走 UDS 协议，串口通道走 LTM 协议（IAP），升级期间帧表格自动旁路。",
        "6、点击 '图表显示' 打开图表控制器：波形/频谱/波形+频谱/XY 四类视图，支持拖拽/缩放；'数据导出'可按时长导出 CSV。",
        "7、控制台可直接输入文本指令发给下位机（LTM 文本指令），'设置'菜单可切换 UTF-8/GBK 编码。",
        "8、菜单 '数据分析 → 打开日志分析器'（Ctrl+L）分析 .log/.db3；",
    };
    ui->plainTextEdit->appendPlainText(helpLines.join("\n"));
}

// ============================================================
// 控件逻辑
// ============================================================
void ConsoleWidget::Private::onSend()
{
    const QByteArray sendData = buildSendData();
    if (sendData.isEmpty()) return;

    emit q->sendRequested(sendData);                    // MainWindow 接给面板直发（含周期发送）
    setInfo(m_hexSendMode ? "十六进制数据发送成功" : "原始数据发送成功");
}

void ConsoleWidget::Private::onClear()
{
    clear();
}

void ConsoleWidget::Private::onSave()
{
    if (ui->plainTextEdit->toPlainText().isEmpty()) {
        setInfo("没有内容可保存！");
        return;
    }

    const QString appPath = QCoreApplication::applicationDirPath();
    const QString defaultFileName = QString("console_log_%1.txt")
                                        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    const QString filePath = QDir(appPath).filePath(defaultFileName);
    const QString fileName = QFileDialog::getSaveFileName(
        q, "保存控制台输出", filePath, "文本文件 (*.txt);;所有文件 (*)");
    if (fileName.isEmpty()) { setInfo("保存已取消"); return; }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setInfo("无法创建文件: " + file.errorString());
        return;
    }
    QTextStream out(&file);
    out << ui->plainTextEdit->toPlainText();
    file.close();
    setInfo("控制台输出已保存至: " + fileName);
}

void ConsoleWidget::Private::onHelp()
{
    showHelp();
}

// ============================================================
// 接收显示
// ============================================================
void ConsoleWidget::Private::displayText(const QByteArray &data)
{
    if (!m_recv) return;                                // 暂停控制台接收
    m_rawReceivedData.append(data);

    if (m_hexShowMode) {
        appendText(data.toHex(' ').toUpper() + "\n");
    } else {
        appendText(m_decoder->decode(data));            // 数据解码
    }
}

// ============================================================
// 助手
// ============================================================
QString ConsoleWidget::Private::toHexDisplay(const QString &rawText) const
{
    return rawText.toUtf8().toHex(' ').toUpper();
}

QString ConsoleWidget::Private::fromHexDisplay(const QString &hexDisplay) const
{
    QString str = hexDisplay;
    str.remove(' ');                                    // 移除空格
    return QString::fromUtf8(QByteArray::fromHex(str.toUtf8()));
}

QString ConsoleWidget::Private::decodeTerminal(const QByteArray &data) const
{
    return (m_codec == 1) ? QString::fromLocal8Bit(data) : QString::fromUtf8(data);
}

QByteArray ConsoleWidget::Private::buildSendData()
{
    QByteArray sendData;
    if (m_hexSendMode) {                                // 16进制发送模式下，编辑框里就是16进制文本
        QString hexStr = ui->editCMD->text();
        hexStr.remove(' ');
        sendData = QByteArray::fromHex(hexStr.toUtf8());
        if (sendData.isEmpty() && !hexStr.isEmpty()) {
            setInfo("无效的十六进制输入");
            return QByteArray();
        }
    } else {
        sendData = ui->editCMD->text().toUtf8();
    }
    if (m_newLine && !sendData.isEmpty())
        sendData.append("\r\n");
    return sendData;
}

void ConsoleWidget::Private::appendText(const QString &text)
{
    QTextCursor cursor = ui->plainTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    ui->plainTextEdit->setTextCursor(cursor);
}

void ConsoleWidget::Private::updateDisplay()
{
    ui->plainTextEdit->clear();
    if (m_hexShowMode) {
        QString hexStr = m_rawReceivedData.toHex(' ').toUpper();
        hexStr.replace("0D 0A", "0D 0A\n");             // 应用换行规则
        appendText(hexStr);
    } else {
        QStringDecoder dec(m_codec == 1 ? QStringConverter::System : QStringConverter::Utf8);
        appendText(dec.decode(m_rawReceivedData));
    }
}

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
ConsoleWidget::ConsoleWidget(QWidget *parent)
    : QGroupBox(parent)
    , pimpl(new Private(this))
{
    pimpl->setup();
}

ConsoleWidget::~ConsoleWidget()
{
    delete pimpl;
}

void ConsoleWidget::connectManager(SerialManager *manager)
{
    pimpl->m_manager = manager;
}

void ConsoleWidget::setStatusBar(StatusBar *bar)     { pimpl->m_bar = bar; }
void ConsoleWidget::setCodec(int codec)              { pimpl->setCodec(codec); }
void ConsoleWidget::showHelp()                       { pimpl->showHelp(); }
void ConsoleWidget::appendText(const QString &text)  { pimpl->appendText(text); }
void ConsoleWidget::appendReceived(const QByteArray &data) { pimpl->displayText(data); }

void ConsoleWidget::setHexSend(bool on)  { pimpl->setHexSend(on); }
void ConsoleWidget::setNewLine(bool on)  { pimpl->setNewLine(on); }
void ConsoleWidget::setHexShow(bool on)  { pimpl->setHexShow(on); }
void ConsoleWidget::setPaused(bool on)   { pimpl->setPaused(on); }
void ConsoleWidget::clear()              { pimpl->clear(); }

void ConsoleWidget::on_btnSend_clicked()     { pimpl->onSend(); }
void ConsoleWidget::on_btnClearRev_clicked() { pimpl->onClear(); }
void ConsoleWidget::on_btnSaveCmd_clicked()  { pimpl->onSave(); }
void ConsoleWidget::on_btnHelp_clicked()     { pimpl->onHelp(); }
