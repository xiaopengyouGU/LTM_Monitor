#ifndef ETHERNET_MANAGER_H
#define ETHERNET_MANAGER_H

#include <QObject>
#include <QStringList>
#include "ethernet_def.h"

#if defined(ETHERNET_LIBRARY)
#  define ETHERNET_EXPORT Q_DECL_EXPORT
#else
#  define ETHERNET_EXPORT Q_DECL_IMPORT
#endif

// 网口管理器（传输层）：只做 TCP Client 的连接、字节收发和状态上报。
// 协议解析由 DataHub 负责，网口模块不知道 LTM、Modbus 等应用协议。
class ETHERNET_EXPORT EthernetManager : public QObject
{
    Q_OBJECT
public:
    explicit EthernetManager(QObject *parent = nullptr);
    ~EthernetManager();

    static QStringList localAddresses();    // 本机可用 IPv4 地址列表

    void start();                           // 启动网口工作线程
    void stop();                            // 停止网口工作线程
    void open(const EthernetConfig &config);// 主动连接设备
    void close();                           // 关闭连接
    void send(const QByteArray &bytes);     // 发送原始字节

signals:
    void ethernetDataUpdated(const QByteArray &bytes);
    void ethernetOpened(bool success, const QString &msg);
    void ethernetClosed();
    void ethernetError(const QString &msg);
    void ethernetOnlineChanged(bool online);

private:
    class Private;
    Private *pimpl = nullptr;
};

#endif // ETHERNET_MANAGER_H