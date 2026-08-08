#ifndef CANFD_MANAGER_H
#define CANFD_MANAGER_H

#include <QObject>
#include "canfd_def.h"

// CAN-FD 管理器：唯一与用户交互的模块，内部自动创建独立工作线程
class CANFD_EXPORT CanfdManager : public QObject
{
    Q_OBJECT
public:
    explicit CanfdManager(QObject *parent = nullptr);
    ~CanfdManager();

    void start();                               // 启动管理器（创建并启动工作线程）
    void stop();                                // 停止管理器
    void open(const CanfdConfig &config);       // 打开设备并初始化通道
    void close();                               // 关闭设备
    void send(const CanfdFrame &frame);         // 发送一帧（自动区分 CAN / CAN-FD）
    void sendBatch(const CanfdFrame &base, int count, int intervalMs, bool idInc);   // 批量/周期发送（Worker 线程定时）
    void stopSend();                            // 停止批量/周期发送
    void setPollInterval(int ms);               // 设置接收轮询周期
    bool isActive();                            // 设备是否在线
    uint32_t rxFrameCount();                    // 累计接收帧数（自打开设备起）
    uint32_t txFrameCount();                    // 累计发送帧数（自打开设备起）

signals:
    void canfdDataUpdated(const QList<CanfdFrame> &frames);   // 收到一批数据
    void canfdOpened(bool success, const QString &msg);
    void canfdClosed();
    void canfdError(const QString &msg);
    void canfdOnlineChanged(bool online);                // 设备在线状态变化（热插拔检测）
    void canfdBusError(uint32_t errCode, int channel);   // 总线错误
    void framesSent(const QList<CanfdFrame> &frames);    // 已发送帧（Tx 回显）

private:
    class Private;
    Private *pimpl;                             // 采用 Pimpl 设计模式开发
};

#endif // CANFD_MANAGER_H
