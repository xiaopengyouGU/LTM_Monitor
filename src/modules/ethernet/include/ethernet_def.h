#ifndef ETHERNET_DEF_H
#define ETHERNET_DEF_H

#include <QMetaType>
#include <QString>

// 网口配置：当前仅支持上位机主动连接设备的 TCP Client 模式
struct EthernetConfig
{
    QString host;                 // 设备 IP 或域名
    quint16 port = 5000;          // 设备端 TCP 监听端口
    QString localAddress;         // 本机绑定地址；空字符串表示自动选择
};

Q_DECLARE_METATYPE(EthernetConfig)

#endif // ETHERNET_DEF_H