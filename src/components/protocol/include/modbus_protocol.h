#ifndef MODBUS_PROTOCOL_H
#define MODBUS_PROTOCOL_H

#include <QByteArray>
#include <QMetaType>
#include <cstdint>

#if defined(PROTOCOL_LIBRARY)
#  define MODBUS_PROTOCOL_EXPORT Q_DECL_EXPORT
#else
#  define MODBUS_PROTOCOL_EXPORT Q_DECL_IMPORT
#endif

// Modbus RTU 轮询配置（随协议组件走，与介质无关）
struct ModbusConfig {
    uint8_t  slave = 1;          // 从站地址
    uint8_t  func  = 0x03;       // 功能码（默认读保持寄存器）
    uint16_t startReg = 0;       // 起始寄存器
    uint16_t quantity = 4;       // 寄存器数量
    int      intervalMs = 30;    // 轮询间隔
};

Q_DECLARE_METATYPE(ModbusConfig)

// Modbus RTU 协议解析
// 帧结构：slave(1) + func(1) + data(N) + CRC16(2)
class MODBUS_PROTOCOL_EXPORT ModbusProtocol
{
public:
    ModbusProtocol();
    ~ModbusProtocol();

    void init();                                        // 清空重组缓冲
    QByteArray package(const ModbusConfig &cfg, const QByteArray &data) const;   // 组请求帧
    void receive(const QByteArray &bytes);              // 字节流入环形缓冲
    bool process(ModbusConfig &cfg, QByteArray &data);  // 解析一帧响应（异常时 cfg.func|0x80，data=异常码）

private:
    Q_DISABLE_COPY(ModbusProtocol)
    class Private;
    Private *pimpl;
};

#endif // MODBUS_PROTOCOL_H
