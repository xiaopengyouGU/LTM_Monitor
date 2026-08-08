#include "canfd_worker.h"
#include "canfd_controller.h"

#include <QDateTime>
#include <QTimer>

CanfdWorker::CanfdWorker(QObject *parent)
    : QObject(parent)
{
    m_controller = new CanfdController(this);   // 与 worker 同线程

    m_timer = new QTimer(this);                 // 轮询接收定时器
    m_timer->setTimerType(Qt::CoarseTimer);
    m_timer->setInterval(m_config.pollIntervalMs);
    m_timer->setSingleShot(false);              // 定时器周期调用
    m_timer->stop();
    connect(m_timer, &QTimer::timeout, this, &CanfdWorker::do_poll_timeout);

    // 发送任务定时器：工作在 Worker 线程，高频发送不占用 UI 线程
    m_sendTimer = new QTimer(this);
    m_sendTimer->setTimerType(Qt::PreciseTimer);
    m_sendTimer->setSingleShot(false);
    m_sendTimer->stop();
    connect(m_sendTimer, &QTimer::timeout, this, &CanfdWorker::do_sendTimer_timeout);

    connect(m_controller, &CanfdController::opened, this, &CanfdWorker::do_controllerOpened);
    connect(m_controller, &CanfdController::closed, this, &CanfdWorker::do_controllerClosed);
    connect(m_controller, &CanfdController::errorOccurred, this, &CanfdWorker::do_controllerError);
}

CanfdWorker::~CanfdWorker() = default;

void CanfdWorker::start()
{
    if (m_opened)
        m_timer->start(m_config.pollIntervalMs);
}

void CanfdWorker::stop()
{
    m_timer->stop();
}

void CanfdWorker::setPollInterval(int ms)
{
    if (ms < MIN_POLL_TIME)
        ms = MIN_POLL_TIME;
    m_config.pollIntervalMs = ms;
    if (m_opened)
        m_timer->start(ms);
}

void CanfdWorker::open(const CanfdConfig &config)
{
    close();                        // 先复位
    m_config = config;
    m_controller->open(config);     // 结果通过 opened 信号返回
}

void CanfdWorker::close()
{
    m_timer->stop();
    m_opened = false;
    m_controller->close();          // 内部根据实际状态决定是否发 closed 信号
}

void CanfdWorker::send(const CanfdFrame &frame)
{
    if (!m_opened) {
        emit canfdError(QString("CAN-FD 设备未打开"));
        return;
    }
    m_controller->transmit(frame.channel, frame);
    CanfdFrame f = frame;
    f.timestampEpochMs = QDateTime::currentMSecsSinceEpoch();
    emit framesSent({f});
}

void CanfdWorker::do_controllerOpened(bool success, const QString &msg)
{
    m_opened = success;
    if (success) {
        m_online = true;                            // 重置在线状态和计数器
        m_activeCheckCount = 0;
        m_lastErr[0] = m_lastErr[1] = 0;            // 重置错误去重/节流状态
        m_lastErrTime[0] = m_lastErrTime[1] = 0;
        m_timer->start(m_config.pollIntervalMs);
    }
    emit canfdOpened(success, msg);
}

void CanfdWorker::do_controllerClosed()
{
    m_opened = false;
    emit canfdClosed();
}

void CanfdWorker::do_controllerError(const QString &msg)
{
    emit canfdError(msg);
}

void CanfdWorker::do_poll_timeout()
{
    if (!m_opened)      return;                     

    // 复用轮询定时器做热插拔检测：按轮询周期分频，约每 1s 检查一次在线状态
    int divisor = qMax(1, 1000 / m_config.pollIntervalMs);
    if (++m_activeCheckCount % divisor == 0) {
        // 热插拔检测
        bool online = m_controller->isActive();
        if (online != m_online) {
            m_online = online;
            emit canfdOnlineChanged(online);
        }
        // 总线错误检测：去重 + 同码 60s 节流（读后清零，非零即报）
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int ch = 0; ch < m_controller->channelCount() && ch < 2; ch++) {
            uint32_t err = m_controller->channelErrorCode(ch);
            if (err == 0)
                continue;
            if (err != m_lastErr[ch] || (now - m_lastErrTime[ch] >= 60000)) {
                m_lastErr[ch] = err;
                m_lastErrTime[ch] = now;
                emit canfdBusError(err, ch);
            }
        }
    }

    QList<CanfdFrame> frames;                       // 单次轮询的全部帧
    for (int ch = 0; ch < m_controller->channelCount(); ch++) {
        QList<CanfdFrame> batch;
        m_controller->receive(ch, batch);
        frames += batch;
    }
    if (!frames.isEmpty())
        emit canfdDataUpdated(frames);              // 一次信号发一批，避免逐帧跨线程洪峰
}

bool CanfdWorker::isActive()
{
    return m_controller->isActive();
}

uint32_t CanfdWorker::rxFrameCount()
{
    return m_controller->rxFrameCount();
}

uint32_t CanfdWorker::txFrameCount()
{
    return m_controller->txFrameCount();
}
void CanfdWorker::startSend(const CanfdFrame &base, int count, int intervalMs, bool idInc)
{
    m_sendTimer->stop();
    m_sendBase = base;
    m_sendId = base.rawId();
    m_idInc = idInc;
    m_sendRemain = count;

    if (!m_opened) {
        emit canfdError(QString("CAN-FD 设备未打开"));
        m_sendRemain = 0;
        return;
    }

    if (intervalMs <= 0) {
        // 间隔 0：立即连发，整批一次发送、一次回显
        QList<CanfdFrame> sent;
        while (m_sendRemain > 0)
            sent.append(sendOneFrame());
        if (!sent.isEmpty())
            emit framesSent(sent);
    } else {
        emit framesSent({sendOneFrame()});      // 先发第一帧并回显
        if (m_sendRemain > 0)
            m_sendTimer->start(intervalMs);     // 再按间隔继续
    }
}

void CanfdWorker::stopSend()
{
    m_sendTimer->stop();
    m_sendRemain = 0;
}

void CanfdWorker::do_sendTimer_timeout()
{
    if (m_sendRemain <= 0) {
        m_sendTimer->stop();
        return;
    }
    emit framesSent({sendOneFrame()});
    if (m_sendRemain <= 0)
        m_sendTimer->stop();
}

CanfdFrame CanfdWorker::sendOneFrame()
{
    CanfdFrame f = m_sendBase;
    f.id = CANFD_MAKE_ID(m_sendId, f.isEff(), 0, 0);
    if (m_opened) {
        m_controller->transmit(f.channel, f);
        f.timestampEpochMs = QDateTime::currentMSecsSinceEpoch();   // 发送时刻的系统时间
    }
    if (m_idInc)
        m_sendId++;
    m_sendRemain--;
    return f;
}