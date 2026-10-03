#ifndef ETHERNET_WORKER_H
#define ETHERNET_WORKER_H

#include <QObject>
#include "ethernet_def.h"

#if defined(ETHERNET_LIBRARY)
#  define ETHERNET_EXPORT Q_DECL_EXPORT
#else
#  define ETHERNET_EXPORT Q_DECL_IMPORT
#endif

class QTcpSocket;
class QTimer;

// 网口 Worker：运行在工作线程中，唯一持有 QTcpSocket。
class ETHERNET_EXPORT EthernetWorker : public QObject
{
    Q_OBJECT
public:
    explicit EthernetWorker(QObject *parent = nullptr);
    ~EthernetWorker();

signals:
    void ethernetDataUpdated(const QByteArray &bytes);
    void ethernetOpened(bool success, const QString &msg);
    void ethernetClosed();
    void ethernetError(const QString &msg);
    void ethernetOnlineChanged(bool online);

public slots:
    void open(const EthernetConfig &config);
    void close();
    void send(const QByteArray &bytes);

private slots:
    void do_connected();
    void do_disconnected();
    void do_readyRead();
    void do_errorOccurred(int socketError);
    void do_connectTimeout();

private:
    QTcpSocket *m_socket = nullptr;
    QTimer     *m_connectTimer = nullptr;
    bool        m_connected = false;
    bool        m_connectPending = false;
};

#endif // ETHERNET_WORKER_H