#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include <QObject>
#include <QByteArray>

#if defined(UDS_SERVER_LIBRARY)
#  define UDS_SERVER_EXPORT Q_DECL_EXPORT
#else
#  define UDS_SERVER_EXPORT Q_DECL_IMPORT
#endif

// UdsServer：硬件无关的 UDS 固件升级协议引擎（纯逻辑，无 UI、无传输层依赖）
// 传输层操作：
//   - 收到 sendRequest 信号 → 打包成 ISO-TP SF_CanFD 单帧发出；
//   - 收到响应帧 → 解出载荷后调用 onResponse() 喂回。
// 与 BootLoader（LtMotorLib BootLoader/IAP-UDS）严格对齐。
class UDS_SERVER_EXPORT UdsServer : public QObject
{
    Q_OBJECT
public:
    explicit UdsServer(QObject *parent = nullptr);
    ~UdsServer();

    // 配置（start 前设置，均有默认值）
    void setAppBase(quint32 addr);      // App 起始地址（默认 0x02008000）
    void setChunkSize(int bytes);       // 0x36 每块数据长度（默认 60，CAN-FD 单帧上限）
    void setRxTimeout(int ms);          // 单请求超时（默认 1000）
    void setMaxRetry(int n);            // 单请求最大重试（默认 3）

    bool isActive() const;              // 升级进行中

public slots:
    void start(const QByteArray &firmware);     // 开始升级（自动从编程会话发起）
    void stop();                                // 用户中止
    void onResponse(const QByteArray &resp);    // 传输层喂入一帧 UDS 响应载荷

signals:
    void sendRequest(const QByteArray &payload);   // 请求传输层发送（SF_CanFD 打包由传输层负责）
    void progressChanged(int percent, const QString &stateText);
    void logMessage(const QString &msg);
    void finished(bool ok, const QString &msg);
    void activeChanged(bool active);

private:
    class Private;
    Private *pimpl;
};

#endif // UDS_SERVER_H
