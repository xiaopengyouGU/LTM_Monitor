#ifndef ETHERNET_MANAGER_PRIVATE_H
#define ETHERNET_MANAGER_PRIVATE_H

#include "ethernet_manager.h"

#if defined(ETHERNET_LIBRARY)
#  define ETHERNET_EXPORT Q_DECL_EXPORT
#else
#  define ETHERNET_EXPORT Q_DECL_IMPORT
#endif

class EthernetWorker;
class QThread;

class ETHERNET_EXPORT EthernetManager::Private : public QObject
{
    Q_OBJECT
public:
    explicit Private(EthernetManager *parent);
    ~Private();

    void start();
    void stop();
    void open(const EthernetConfig &config);
    void close();
    void send(const QByteArray &bytes);

signals:
    void openEthernet(const EthernetConfig &config);
    void closeEthernet();
    void sendData(const QByteArray &bytes);

private slots:
    void do_ethernetDataUpdated(const QByteArray &bytes);
    void do_ethernetOpened(bool success, const QString &msg);
    void do_ethernetClosed();
    void do_ethernetError(const QString &msg);
    void do_ethernetOnlineChanged(bool online);

private:
    EthernetManager *m_manager = nullptr;
    QThread         *m_thread = nullptr;
    EthernetWorker  *m_worker = nullptr;
};

#endif // ETHERNET_MANAGER_PRIVATE_H