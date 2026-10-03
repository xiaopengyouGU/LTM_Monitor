#include "ethernet_manager.h"
#include "ethernet_manager_private.h"
#include "ethernet_worker.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QThread>

QStringList EthernetManager::localAddresses()
{
    QStringList result;
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol)
            continue;
        if (address.isLoopback())
            continue;
        result.append(address.toString());
    }
    result.removeDuplicates();
    return result;
}

EthernetManager::Private::Private(EthernetManager *parent)
    : QObject(parent), m_manager(parent)
{
    static const bool registered = []() {
        qRegisterMetaType<EthernetConfig>("EthernetConfig");
        return true;
    }();
    Q_UNUSED(registered);

    m_worker = new EthernetWorker;
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_worker, &EthernetWorker::ethernetDataUpdated, this, &Private::do_ethernetDataUpdated);
    connect(m_worker, &EthernetWorker::ethernetOpened, this, &Private::do_ethernetOpened);
    connect(m_worker, &EthernetWorker::ethernetClosed, this, &Private::do_ethernetClosed);
    connect(m_worker, &EthernetWorker::ethernetError, this, &Private::do_ethernetError);
    connect(m_worker, &EthernetWorker::ethernetOnlineChanged, this, &Private::do_ethernetOnlineChanged);

    connect(this, &Private::openEthernet, m_worker, &EthernetWorker::open);
    connect(this, &Private::closeEthernet, m_worker, &EthernetWorker::close);
    connect(this, &Private::sendData, m_worker, &EthernetWorker::send);
}

EthernetManager::Private::~Private()
{
    stop();
    delete m_worker;
}

void EthernetManager::Private::start()
{
    m_thread->start();
}

void EthernetManager::Private::stop()
{
    if (m_thread && m_thread->isRunning())
        QMetaObject::invokeMethod(m_worker, &EthernetWorker::close, Qt::BlockingQueuedConnection);
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

void EthernetManager::Private::open(const EthernetConfig &config)
{
    emit openEthernet(config);
}

void EthernetManager::Private::close()
{
    emit closeEthernet();
}

void EthernetManager::Private::send(const QByteArray &bytes)
{
    emit sendData(bytes);
}

void EthernetManager::Private::do_ethernetDataUpdated(const QByteArray &bytes)
{
    emit m_manager->ethernetDataUpdated(bytes);
}

void EthernetManager::Private::do_ethernetOpened(bool success, const QString &msg)
{
    emit m_manager->ethernetOpened(success, msg);
}

void EthernetManager::Private::do_ethernetClosed()
{
    emit m_manager->ethernetClosed();
}

void EthernetManager::Private::do_ethernetError(const QString &msg)
{
    emit m_manager->ethernetError(msg);
}

void EthernetManager::Private::do_ethernetOnlineChanged(bool online)
{
    emit m_manager->ethernetOnlineChanged(online);
}

EthernetManager::EthernetManager(QObject *parent)
    : QObject(parent), pimpl(new Private(this)) {}

EthernetManager::~EthernetManager() = default;

void EthernetManager::start()                        { pimpl->start(); }
void EthernetManager::stop()                         { pimpl->stop(); }
void EthernetManager::open(const EthernetConfig &c)  { pimpl->open(c); }
void EthernetManager::close()                        { pimpl->close(); }
void EthernetManager::send(const QByteArray &bytes)  { pimpl->send(bytes); }