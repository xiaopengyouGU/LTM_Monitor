#include "ethernet_worker.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>

EthernetWorker::EthernetWorker(QObject *parent) : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    m_connectTimer->setInterval(5000);              // 连接超时使用内部默认值，不进 UI

    connect(m_socket, &QTcpSocket::connected, this, &EthernetWorker::do_connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &EthernetWorker::do_disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &EthernetWorker::do_readyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError error) { do_errorOccurred(int(error)); });
    connect(m_connectTimer, &QTimer::timeout, this, &EthernetWorker::do_connectTimeout);
}

EthernetWorker::~EthernetWorker()
{
    close();
}

void EthernetWorker::open(const EthernetConfig &config)
{
    close();

    const QString host = config.host.trimmed();
    if (host.isEmpty()) {
        emit ethernetOpened(false, "设备IP不能为空");
        return;
    }
    if (config.port == 0) {
        emit ethernetOpened(false, "端口号无效");
        return;
    }

    // 空字符串或“自动”都交给操作系统选择出口地址
    const QString local = config.localAddress.trimmed();
    if (!local.isEmpty() && local.compare(QString("自动"), Qt::CaseInsensitive) != 0) {
        const QHostAddress localAddress(local);
        if (localAddress.isNull()) {
            emit ethernetOpened(false, "本机IP无效");
            return;
        }
        if (!m_socket->bind(localAddress)) {
            emit ethernetOpened(false, QString("本机绑定失败：%1").arg(m_socket->errorString()));
            return;
        }
    }

    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);   // TCP_NODELAY
    m_connectPending = true;
    m_connectTimer->start();
    m_socket->connectToHost(host, config.port);
}

void EthernetWorker::close()
{
    m_connectTimer->stop();
    const bool wasConnected = m_connected;
    m_connected = false;
    m_connectPending = false;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->blockSignals(true);
        m_socket->abort();
        m_socket->blockSignals(false);
    }

    if (wasConnected) {
        emit ethernetOnlineChanged(false);
        emit ethernetClosed();
    }
}

void EthernetWorker::send(const QByteArray &bytes)
{
    if (m_connected && !bytes.isEmpty())
        m_socket->write(bytes);
}

void EthernetWorker::do_connected()
{
    m_connectTimer->stop();
    m_connectPending = false;
    m_connected = true;
    emit ethernetOnlineChanged(true);
    emit ethernetOpened(true, "网口连接成功");
}

void EthernetWorker::do_disconnected()
{
    m_connectTimer->stop();

    if (m_connectPending) {
        m_connectPending = false;
        emit ethernetOpened(false, "网口连接失败");
    }
    if (m_connected) {
        m_connected = false;
        emit ethernetOnlineChanged(false);
        emit ethernetClosed();
    }
}

void EthernetWorker::do_readyRead()
{
    const QByteArray bytes = m_socket->readAll();
    if (!bytes.isEmpty())
        emit ethernetDataUpdated(bytes);             // 原始字节上报，协议切帧由 DataHub 完成
}

void EthernetWorker::do_errorOccurred(int socketError)
{
    Q_UNUSED(socketError);
    m_connectTimer->stop();
    const QString msg = m_socket->errorString();

    if (m_connectPending) {
        m_connectPending = false;
        emit ethernetOpened(false, msg);
        return;
    }

    if (m_connected) {
        m_connected = false;
        emit ethernetOnlineChanged(false);
        emit ethernetClosed();
    }
    emit ethernetError(msg);
}

void EthernetWorker::do_connectTimeout()
{
    if (!m_connectPending)
        return;
    m_connectPending = false;
    m_socket->abort();
    emit ethernetOpened(false, "网口连接超时");
}