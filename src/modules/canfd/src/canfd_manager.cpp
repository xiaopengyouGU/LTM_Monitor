#include "canfd_manager.h"
#include "canfd_manager_private.h"
#include "canfd_worker.h"
#include <QThread>

CanfdManager::Private::Private(CanfdManager *parent) : QObject(parent), m_manager(parent)
{
    static const bool registered = []() {
        qRegisterMetaType<CanfdFrame>("CanfdFrame");
        qRegisterMetaType<CanfdConfig>("CanfdConfig");
        qRegisterMetaType<QList<CanfdFrame>>("QList<CanfdFrame>");
        return true;
    }();
    Q_UNUSED(registered);

    m_worker = new CanfdWorker;
    data_thread = new QThread(this);
    m_worker->moveToThread(data_thread);        // 将 Worker 移动到数据线程中

    // worker -> 中转
    connect(m_worker, &CanfdWorker::canfdDataUpdated, this, &Private::do_canfdDataUpdated);
    connect(m_worker, &CanfdWorker::canfdOpened, this, &Private::do_canfdOpened);
    connect(m_worker, &CanfdWorker::canfdClosed, this, &Private::do_canfdClosed);
    connect(m_worker, &CanfdWorker::canfdError, this, &Private::do_canfdError);
    connect(m_worker, &CanfdWorker::framesSent, this, &Private::do_framesSent);
    connect(m_worker, &CanfdWorker::canfdOnlineChanged, this, &Private::do_canfdOnlineChanged);
    connect(m_worker, &CanfdWorker::canfdBusError, this, &Private::do_canfdBusError);

    // 中转 -> worker
    connect(this, &Private::openCanfd,  m_worker, &CanfdWorker::open);
    connect(this, &Private::closeCanfd, m_worker, &CanfdWorker::close);
    connect(this, &Private::sendCanfd,  m_worker, &CanfdWorker::send);
    connect(this, &Private::sendBatchCanfd, m_worker, &CanfdWorker::startSend);
    connect(this, &Private::stopSendCanfd,  m_worker, &CanfdWorker::stopSend);
    connect(this, &Private::setPollIntervalCanfd, m_worker, &CanfdWorker::setPollInterval);

    // 线程启动后启动 worker 的轮询定时器
    connect(data_thread, &QThread::started, m_worker, &CanfdWorker::start);
}

CanfdManager::Private::~Private()
{
    stop();
    delete m_worker;
}

void CanfdManager::Private::start()
{
    data_thread->start();
}

void CanfdManager::Private::stop()
{
    if (data_thread->isRunning())
        QMetaObject::invokeMethod(m_worker, &CanfdWorker::stop, Qt::BlockingQueuedConnection);
    data_thread->quit();
    data_thread->wait();
}

void CanfdManager::Private::open(const CanfdConfig &config)
{
    emit openCanfd(config);
}

void CanfdManager::Private::close()
{
    emit closeCanfd();
}

void CanfdManager::Private::send(const CanfdFrame &frame)
{
    emit sendCanfd(frame);
}


void CanfdManager::Private::sendBatch(const CanfdFrame &base, int count, int intervalMs, bool idInc)
{
    emit sendBatchCanfd(base, count, intervalMs, idInc);
}

void CanfdManager::Private::stopSend()
{
    emit stopSendCanfd();
}

void CanfdManager::Private::setPollInterval(int ms)
{
    emit setPollIntervalCanfd(ms);
}

// 信号中转
void CanfdManager::Private::do_canfdDataUpdated(const QList<CanfdFrame> &frames)
{
    emit m_manager->canfdDataUpdated(frames);
}

void CanfdManager::Private::do_canfdOpened(bool success, const QString &msg)
{
    emit m_manager->canfdOpened(success, msg);
}

void CanfdManager::Private::do_canfdClosed()
{
    emit m_manager->canfdClosed();
}

void CanfdManager::Private::do_canfdError(const QString &msg)
{
    emit m_manager->canfdError(msg);
}

void CanfdManager::Private::do_framesSent(const QList<CanfdFrame> &frames)
{
    emit m_manager->framesSent(frames);
}

void CanfdManager::Private::do_canfdOnlineChanged(bool online)
{
    emit m_manager->canfdOnlineChanged(online);
}

void CanfdManager::Private::do_canfdBusError(uint32_t errCode, int channel)
{
    emit m_manager->canfdBusError(errCode, channel);
}

bool CanfdManager::Private::isActive()
{
    bool ret = false;
    if (data_thread->isRunning())
        QMetaObject::invokeMethod(m_worker, &CanfdWorker::isActive, Qt::BlockingQueuedConnection, &ret);
    return ret;
}

uint32_t CanfdManager::Private::rxFrameCount()
{
    uint32_t ret = 0;
    if (data_thread->isRunning())
        QMetaObject::invokeMethod(m_worker, &CanfdWorker::rxFrameCount, Qt::BlockingQueuedConnection, &ret);
    return ret;
}

uint32_t CanfdManager::Private::txFrameCount()
{
    uint32_t ret = 0;
    if (data_thread->isRunning())
        QMetaObject::invokeMethod(m_worker, &CanfdWorker::txFrameCount, Qt::BlockingQueuedConnection, &ret);
    return ret;
}

// ========== CanfdManager 公共接口实现 ==========
CanfdManager::CanfdManager(QObject *parent)
    : QObject(parent), pimpl(new Private(this)) {}

CanfdManager::~CanfdManager() = default;   // Qt 负责内存管理

void CanfdManager::start()           { pimpl->start(); }
void CanfdManager::stop()            { pimpl->stop(); }
void CanfdManager::open(const CanfdConfig &config) { pimpl->open(config); }
void CanfdManager::close()           { pimpl->close(); }
void CanfdManager::send(const CanfdFrame &frame)   { pimpl->send(frame); }
void CanfdManager::sendBatch(const CanfdFrame &base, int count, int intervalMs, bool idInc) { pimpl->sendBatch(base, count, intervalMs, idInc); }
void CanfdManager::stopSend()            { pimpl->stopSend(); }
void CanfdManager::setPollInterval(int ms)         { pimpl->setPollInterval(ms); }
bool CanfdManager::isActive()         { return pimpl->isActive(); }
uint32_t CanfdManager::rxFrameCount() { return pimpl->rxFrameCount(); }
uint32_t CanfdManager::txFrameCount() { return pimpl->txFrameCount(); }