#ifndef UDS_WIDGET_H
#define UDS_WIDGET_H

#include <QWidget>
#include "canfd_def.h"

#if defined(UDS_WIDGET_LIBRARY)
#  define UDS_WIDGET_EXPORT Q_DECL_EXPORT
#else
#  define UDS_WIDGET_EXPORT Q_DECL_IMPORT
#endif

class CanfdManager;
class SerialManager;
class RecordManager;

// UDS 固件升级界面：UI + CAN-FD 传输绑定。
// 协议引擎（状态机/CRC/重试）由组件 UdsServer 承担（components/uds_server），
// 本界面只做三件事：
//   1. 配置与固件选择（ID / App 地址 / bin 文件）；
//   2. 传输层：sendRequest 信号 → ISO-TP SF_CanFD 打包发送；收帧 → 解载荷喂回 onResponse；
//   3. 展示：进度/日志/记录模块/轮询加速/中转站旁路。
class UDS_WIDGET_EXPORT UdsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UdsWidget(QWidget *parent = nullptr);
    ~UdsWidget();

    void connectManager(CanfdManager *manager);             // 绑定 CAN-FD 管理器（发送用）
    void connectSerialManager(SerialManager *manager);      // 绑定串口管理器（LTM 协议通道）
    void setRecordManager(RecordManager *manager);  // 绑定日志记录模块（升级过程写入日志）
    void setProtocol(int index);                    // 预选协议：0=CAN-FD / 1=LTM（MainWindow 跳转时设置）

signals:
    void upgradeActiveChanged(bool active);                 // 升级开始/结束（MainWindow 接中转站旁路显示）

public slots:
    void onFrameReceived(const QList<CanfdFrame> &frames);  // 由中转站 canfdRawReceived 驱动（MainWindow 连接）

private slots:
    void on_btnSelect_clicked();
    void on_btnStart_clicked();
    void on_btnStop_clicked();
    void on_btnClear_clicked();

private:
    Q_DISABLE_COPY(UdsWidget)
    class Private;
    Private *pimpl = nullptr;
};

#endif // UDS_WIDGET_H



