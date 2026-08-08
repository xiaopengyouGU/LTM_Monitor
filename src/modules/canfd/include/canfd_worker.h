#ifndef CANFD_WORKER_H
#define CANFD_WORKER_H

#include <QObject>
#include "canfd_def.h"

class QTimer;
class CanfdController;

// CAN-FD 工作对象：与控制器同线程运行，负责周期轮询接收。
// CAN-FD 数据属于异步事件（上位机无法预知到达时刻），故逐帧上报。
class CANFD_EXPORT CanfdWorker : public QObject
{
    Q_OBJECT
public:
    explicit CanfdWorker(QObject *parent = nullptr);
    ~CanfdWorker();

    bool isActive();                        // 设备是否在线（工作线程内调用）
    uint32_t rxFrameCount();                // 累计接收帧数
    uint32_t txFrameCount();                // 累计发送帧数

signals:
    void canfdDataUpdated(const QList<CanfdFrame> &frames);   // 收到一批数据（单次轮询）
    void canfdOpened(bool success, const QString &msg);
    void canfdClosed();
    void canfdError(const QString &msg);
    void canfdOnlineChanged(bool online);   // 设备在线状态变化（热插拔检测，分频轮询）
    void canfdBusError(uint32_t errCode, int channel);   // 总线错误（分频轮询检测）
    void framesSent(const QList<CanfdFrame> &frames);       // 已发送帧（Tx 回显，Worker 线程发出）

public slots:
    void start();
    void stop();
    void setPollInterval(int ms);
    void open(const CanfdConfig &config);
    void close();
    void send(const CanfdFrame &frame);                     // 单帧发送（立即）
    void startSend(const CanfdFrame &base, int count, int intervalMs, bool idInc);   // 批量/周期发送任务（本线程定时）
    void stopSend();                                        // 停止发送任务

private slots:
    void do_poll_timeout();
    void do_controllerOpened(bool success, const QString &msg);
    void do_controllerClosed();
    void do_controllerError(const QString &msg);
    void do_sendTimer_timeout();

private:
    CanfdController *m_controller = nullptr;
    QTimer          *m_timer = nullptr;
    QTimer          *m_sendTimer = nullptr;   // 发送任务定时器（工作线程内运行）
    CanfdConfig      m_config;
    bool             m_opened = false;

    CanfdFrame      m_sendBase;               // 发送任务基础帧
    quint32         m_sendId = 0;             // 当前发送 ID
    int             m_sendRemain = 0;         // 剩余待发送帧数
    bool            m_idInc = false;          // 是否 ID 自增
    CanfdFrame sendOneFrame();                // 发送一帧（含 ID 自增）

    int  m_activeCheckCount = 0;              // 热插拔检测计数（分频轮询定时器）
    bool m_online = true;                     // 上次检测的在线状态

    uint32_t m_lastErr[2] = {0, 0};           // 上次上报的通道错误码（去重）
    qint64   m_lastErrTime[2] = {0, 0};       // 上次上报时间（同码 60s 节流）
};

#endif // CANFD_WORKER_H
