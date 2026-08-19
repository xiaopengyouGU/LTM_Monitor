#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include <QByteArray>
#include <QObject>
#include <functional>

#include "modbus_protocol.h"

#if defined(PROTOCOL_LIBRARY)
#  define MODBUS_MASTER_EXPORT Q_DECL_EXPORT
#else
#  define MODBUS_MASTER_EXPORT Q_DECL_IMPORT
#endif

// Modbus RTU 主站事务器（components 层）
// 职责：轮询定时、事务队列、响应看门狗——组合 ModbusProtocol，与传输介质解耦
// 传输注入：setSendCallback 提供"发送原始字节"能力；接收字节经 receive() 喂入
class MODBUS_MASTER_EXPORT ModbusMaster : public QObject
{
    Q_OBJECT
public:
    explicit ModbusMaster(QObject *parent = nullptr);
    ~ModbusMaster();

    void setSendCallback(std::function<void(const QByteArray &)> cb);   // 注入发送字节能力
    void receive(const QByteArray &bytes);                              // 喂入响应字节（事务泵驱动）
    void start(const ModbusConfig &cfg);                                // 启动轮询（立即发第一问）
    void stop();                                                        // 停止轮询，清空事务
    void send(const ModbusConfig &cfg, const QByteArray &data);         // 写类请求插队

signals:
    void modbusResponse(const ModbusConfig &cfg, const QByteArray &data);
    void modbusException(const ModbusConfig &cfg, uint8_t code);
    void modbusTimeout(int missCount);

private:
    Q_DISABLE_COPY(ModbusMaster)
    class Private;
    Private *pimpl;
};

#endif // MODBUS_MASTER_H
