#ifndef PID_WIDGET_H
#define PID_WIDGET_H

#include <QWidget>

class SerialManager;
class StatusBar;
class DataHub;

// PID 调试面板（原 MainWindow pidConfig 页）：
// 通道选择 / 目标值 / PID 参数 / 周期发送；实际值由 DataHub 经 setValue 直写内部数组。
// 内部数据（PidData/通道数组/节流状态）全部收敛在 Private（Pimpl）中。
class PidWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PidWidget(QWidget *parent = nullptr);
    ~PidWidget();

    void connectManager(SerialManager *manager);   // 直连串口管理器（发送 PID 指令）
    void connectHub(DataHub *hub);                 // 路由发送：串口优先，否则 CAN-FD
    void setStatusBar(StatusBar *bar);             // 直连状态栏
    void setValue(int ch, float value);            // 强写实际值（DataHub 直调，越界忽略）
    void refreshDisplay();                         // 刷新界面
    void refreshActual();                          // 只刷新实际值（数据更新通知用，不动目标值）

private slots:
    void on_comboCh_currentIndexChanged(int index);
    void on_btnTarget_clicked();
    void on_btnPID_clicked();
    void on_btnPeriod_clicked();

private:
    Q_DISABLE_COPY(PidWidget)
    class Private;
    Private *pimpl = nullptr;
};

#endif // PID_WIDGET_H
