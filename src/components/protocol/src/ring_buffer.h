#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <cstdint>
#include <QtGlobal>

// CRC-16/MODBUS 校验（查表法，LTM / Modbus 协议共用，protocol 组件内部设施）
uint16_t crc16_modbus(const uint8_t *data, uint16_t len);

// 环形缓冲区（protocol 组件内部公共设施）：LTM / Modbus 重组共用
class RingBuffer
{
public:
    explicit RingBuffer(void);      // 默认大小：512字节
    ~RingBuffer();

    void reset();
    uint16_t available() const;
    uint16_t space() const;
    bool write(const uint8_t *data, uint16_t len);
    bool peek(uint16_t offset, uint8_t *data, uint16_t len) const;
    bool consume(uint16_t len);

private:
    Q_DISABLE_COPY(RingBuffer)
    class Private;
    Private *pimpl;
};

#endif // RING_BUFFER_H
