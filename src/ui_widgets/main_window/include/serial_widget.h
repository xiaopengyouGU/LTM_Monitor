#ifndef SERIAL_WIDGET_H
#define SERIAL_WIDGET_H

#include <QWidget>
#include <QByteArray>

class SerialManager;
class ConsoleWidget;
class StatusBar;
class DataHub;

// 串口调试控制面板：
// 协议切换 / 周期发送 / hex 收发模式 / 暂停清除 / 发送文件 / IAP 入口。
// 直连 SerialManager；控制台相关控件（hex 显示/新行/暂停/清除）直调 ConsoleWidget。
class SerialWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SerialWidget(QWidget *parent = nullptr);
    ~SerialWidget();

    void connectManager(SerialManager *manager);   // 直连串口管理器
    void connectHub(DataHub *hub);                 // 路由发送：串口优先，否则 CAN-FD
    void setConsole(ConsoleWidget *console);       // 直连控制台（hex/新行/暂停/清除）
    void setStatusBar(StatusBar *bar);             // 直连状态栏（免 MainWindow 中转）
    void setProtocol(int index);                   // 外部预选协议（IAP 入口：LTM）
    void sendText(const QByteArray &data);         // 发送指令（含 Modbus 检查与周期发送）

signals:
    void iapUpgradeRequested();                    // MainWindow 跳转 UDS 页面（LTM）

private slots:
    void on_comboProt_currentIndexChanged(int index);
    void on_chkSendPeriod_stateChanged(int arg1);
    void on_chkHexSend_stateChanged(int arg1);
    void on_chkSendNewL_stateChanged(int arg1);
    void on_chkHexShow_stateChanged(int arg1);
    void on_btnStopRecv_clicked();
    void on_btnClearRecv_clicked();
    void on_btnSendFile_clicked();
    void on_btnIapUpgrade_clicked();

private:
    Q_DISABLE_COPY(SerialWidget)
    class Private;
    Private *pimpl = nullptr;
};

#endif // SERIAL_WIDGET_H
