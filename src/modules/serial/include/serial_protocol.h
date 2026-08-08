#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <QObject>
#include "serial_def.h"

// 帧前缀长度：LTM 与 Modbus 均为 4 字节（前缀 + 数据 + CRC2 同构）
#define HEAD_SIZE 4

#if defined(SERIAL_LIBRARY)
#  define SERIAL_EXPORT Q_DECL_EXPORT
#else
#  define SERIAL_EXPORT Q_DECL_IMPORT
#endif

class SERIAL_EXPORT SerialProtocol{
    // 本对象不需要使用信号与槽机制
public:
    explicit SerialProtocol(); 
    void init();                                         // 初始化
    // LTM 协议操作接口
    bool process(uint8_t &data_type, QByteArray& data);
    void package(uint8_t data_type, const QByteArray& data);       // 将待发送数据打包
    QByteArray getRawData();                                       // 获取打包后的原始数据，用于串口发送
    void receive(const QByteArray& raw_data);                      // 将数据放入环形缓冲区
    // Modbus RTU 协议操作接口，同样调用 getRawData() 获取打包后的原始数据
    bool process(ModbusConfig &cfg, QByteArray &data);             // 从环形缓冲解析一帧响应（异常时 cfg.func 置 |0x80，data=异常码）
    void package(const ModbusConfig &cfg, const QByteArray& data); // 组 Modbus 请求帧（含 CRC）→ rawData
    
private:
    // 常量定义与 LTM协议
    static constexpr uint32_t FRAME_HEADER = 0xA5B9;    // LTM 协议帧头， 小端存储（2字节）：B9 A5，极其冷门
    static constexpr uint16_t RB_SIZE = 512;            // 环形缓冲区大小
    static constexpr uint16_t MAX_DATA_SIZE = 128;      // 支持的最大数据长度

    // 环形缓冲区结构
    struct RingBuffer {
        uint8_t buffer[RB_SIZE];
        uint16_t head;
        uint16_t tail;
    };

private:
    RingBuffer rb;
    uint8_t  send_buf[MAX_DATA_SIZE+6];                 // 发送缓冲区（固定帧结构：6字节）
    uint16_t buf_len;                                   // 发送缓冲区的数据长度

    static const uint16_t CRC16_TABLE[256];             // 查表法 CRC16校验
    static uint16_t crc16_check(const uint8_t* data, uint16_t len);
    // 帧解析：各协议注入前缀判定回调，取帧骨架共用
    static int  ltmHeadCheck(const uint8_t *head, const ModbusConfig *cfg);     // LTM 前缀判定
    static int  modbusHeadCheck(const uint8_t *head, const ModbusConfig *cfg);  // Modbus 前缀判定
    bool rb_take_frame(int (*headCheck)(const uint8_t *head, const ModbusConfig *cfg),
                       const ModbusConfig *cfg,
                       uint8_t *frame, int *frameLen);   // 通用取帧骨架
    // 环形缓冲区接口
    void rb_init();
    uint16_t rb_available() const;
    uint16_t rb_space() const;
    bool rb_write(const uint8_t *data, uint16_t len);
    bool rb_peek(uint16_t offset, uint8_t *data, uint16_t len) const;
    bool rb_consume(uint16_t len);
};

#endif // SERIAL_PROTOCOL_H
