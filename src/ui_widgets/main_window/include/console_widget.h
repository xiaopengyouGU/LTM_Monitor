#ifndef CONSOLE_WIDGET_H
#define CONSOLE_WIDGET_H

#include <QGroupBox>
#include <QByteArray>

class SerialManager;
class DataHub;
class StatusBar;

// 串口控制台（原 MainWindow terminal GroupBox）：
// 接收显示（hex/文本、暂停、清除、编码）与指令发送。
// 直连 SerialManager（串口数据更新 → 控制台显示，Qt::QueuedConnection 保证 UI 线程安全）；
// 发送经 sendRequested 信号由 MainWindow 接给 SerialWidget（含周期发送）。
// 内部状态（接收/hex/编码/原始缓冲）全部收敛在 Private（Pimpl）中。
class ConsoleWidget : public QGroupBox
{
    Q_OBJECT
public:
    explicit ConsoleWidget(QWidget *parent = nullptr);
    ~ConsoleWidget();

    void connectManager(SerialManager *manager);        // 直连串口管理器
    void setStatusBar(StatusBar *bar);                  // 直连状态栏（免 MainWindow 中转）
    void setCodec(int codec);                           // 控制台编码：0=UTF-8 / 1=GBK（MainWindow 菜单）
    void showHelp();                                    // 帮助文本（按钮 + 菜单共用）
    void appendText(const QString &text);               // 追加文本（其他模块状态写入控制台）
    void appendReceived(const QByteArray &data);        // 接收文本输入（MainWindow 统一转发，含 hex/编码处理）

public slots:
    void setHexSend(bool on);                           // 编辑框文本 hex <-> 原文
    void setNewLine(bool on);                           // 发送新行状态（面板控件直调）
    void setHexShow(bool on);                           // 控制台刷新显示
    void setPaused(bool on);                            // 暂停接收
    void clear();                                       // 清空控制台 + 原始缓冲

signals:
    void sendRequested(const QByteArray &data);         // 面板直发（MainWindow 接线：sendText）

private slots:
    void on_btnSend_clicked();
    void on_btnClearRev_clicked();
    void on_btnSaveCmd_clicked();
    void on_btnHelp_clicked();

private:
    Q_DISABLE_COPY(ConsoleWidget)
    class Private;
    Private *pimpl = nullptr;
};

#endif // CONSOLE_WIDGET_H
