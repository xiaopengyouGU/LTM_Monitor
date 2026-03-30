#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <QObject>

#if defined(SERIAL_LIBRARY)
#  define SERIAL_EXPORT Q_DECL_EXPORT
#else
#  define SERIAL_EXPORT Q_DECL_IMPORT
#endif

class SERIAL_EXPORT SerialProtocol{
    //本对象不需要使用信号与槽机制
public:
    explicit SerialProtocol(); 
    void init();                   // 初始化
    bool process(uint8_t &data_type, QByteArray& data);
    void package(uint8_t data_type, const QByteArray& data);    //将待发送数据打包
    QByteArray getRawData();                   // 获取打包后的原始数据，用于串口发送
    void receive(const QByteArray& raw_data);  // 接收串口缓冲区的原始数据

private:
    // 常量定义
    static constexpr uint32_t FRAME_HEADER = 0xABCD1234;    // 帧头
    static constexpr uint32_t FRAME_TAILER = 0x5678EFDC;    // 帧尾
    static constexpr uint16_t RB_SIZE = 256;                // 环形缓冲区大小
    static constexpr uint16_t MAX_DATA_SIZE = 80;           // 支持的最大数据长度

    // 帧头结构（1字节对齐）
    #pragma pack(push, 1)
    struct ProtocolHeader {
        uint32_t header;
        uint8_t data_type;
        uint16_t data_len;
    };
    #pragma pack(pop)

    // 环形缓冲区结构
    struct RingBuffer {
        uint8_t buffer[RB_SIZE];
        uint16_t head;
        uint16_t tail;
    };

private:
    QByteArray      rawData;                            // 打包后的原始数据
    // 成员变量
    RingBuffer rb;
    uint8_t send_buf[RB_SIZE];                          // 发送缓冲区
    uint16_t buf_len;                                   // 发送缓冲区的数据长度

    // 私有辅助函数
    static uint16_t crc16_check(const uint8_t* data, uint16_t len);
    static const char* data_type_to_str(uint8_t type);
    bool parse_frame(const uint8_t* buffer, uint16_t recv_len,
                     uint8_t* data_type, QByteArray &data);
    void rb_init();
    uint16_t rb_available() const;
    uint16_t rb_space() const;
    bool rb_write(const uint8_t *data, uint16_t len);
    bool rb_peek(uint16_t offset, uint8_t *data, uint16_t len) const;
    bool rb_consume(uint16_t len);
    bool rb_parse_frame(uint8_t *data_type, QByteArray & data);
};

#endif // SERIAL_PROTOCOL_H