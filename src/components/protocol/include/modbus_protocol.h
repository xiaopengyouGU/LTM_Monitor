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
    uint16_t startReg = 0;       // 起始寄存器 (地址单位：寄存器)
    uint16_t quantity = 2;       // 寄存器数量（一个寄存器 16位）
    int      intervalMs = 30;    // 轮询间隔
    bool     isException = false;  // process() 解析到异常时设为 true
};

Q_DECLARE_METATYPE(ModbusConfig)

// 注：写多个线圈时，用户需手动完成 data的拼接：例如从0x00地址开始写入10个线圈
// ModbusConfig cfg;
// cfg.slave = 0x01;
// cfg.func = 0x0F;
// cfg.startReg = 0x0000;
// cfg.quantity = 1;         // 数据不超过1个寄存器
// // 10 个线圈的状态：ON,OFF,ON,OFF,ON,OFF,ON,OFF,ON,OFF
// // 位打包：第1字节 = 0b10101010 (0xAA)，第2字节 = 0b00000010 (0x02)
// QByteArray data;
// data.append(char(0xAA));  // 线圈 0-7
// data.append(char(0x02));  // 线圈 8-9（高位补0）
// QByteArray frame = protocol.package(cfg, data);

// Modbus RTU 协议解析
// 写多个寄存器(0x10) 和 写多个线圈(0x0F) 都需要字节数字段
// 读取帧结构：slave(1) + func(1) + startReg(2) + quantity(2) + CRC16(2)
// 写入帧结构：slave(1) + func(1) + startReg(2) + quantity(2) + [字节数(1)] + data(N) + CRC16(2)
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
