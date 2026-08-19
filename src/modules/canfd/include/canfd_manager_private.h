#ifndef CANFD_MANAGER_PRIVATE_H
#define CANFD_MANAGER_PRIVATE_H

#include "canfd_manager.h"

#if defined(CANFD_LIBRARY)
#  define CANFD_EXPORT Q_DECL_EXPORT
#else
#  define CANFD_EXPORT Q_DECL_IMPORT
#endif

class CanfdWorker;
class QThread;

class CANFD_EXPORT CanfdManager::Private : public QObject
{
    Q_OBJECT
public:
    Private(CanfdManager *parent);
    ~Private();
    void start();
    void stop();
    void open(const CanfdConfig &config);
    void close();
    void send(const CanfdFrame &frame);
    void sendBatch(const CanfdFrame &base, int count, int intervalMs, bool idInc);
    void stopSend();
    void setPollInterval(int ms);
    bool isActive();
    uint32_t rxFrameCount();
    uint32_t txFrameCount();

signals:
    void openCanfd(const CanfdConfig &config);
    void closeCanfd();
    void sendCanfd(const CanfdFrame &frame);
    void sendBatchCanfd(const CanfdFrame &base, int count, int intervalMs, bool idInc);
    void stopSendCanfd();
    void setPollIntervalCanfd(int ms);

private slots:
    void do_canfdDataUpdated(const QList<CanfdFrame> &frames);
    void do_canfdOpened(bool success, const QString &msg);
    void do_canfdClosed();
    void do_canfdError(const QString &msg);
    void do_framesSent(const QList<CanfdFrame> &frames);
    void do_canfdOnlineChanged(bool online);
    void do_canfdBusError(uint32_t errCode, int channel);

private:
    CanfdManager *m_manager = nullptr;
    QThread      *data_thread = nullptr;
    CanfdWorker  *m_worker  = nullptr;
};

#endif // CANFD_MANAGER_PRIVATE_H
