#include "ltm_protocol.h"
#include "ring_buffer.h"
#include <cstring>

class LtmProtocol::Private
{
public:
    void init() { m_rb.reset(); }
    QByteArray package(uint8_t type, const QByteArray &data) const;
    void receive(const QByteArray &bytes) { m_rb.write((const uint8_t *)bytes.constData(), (uint16_t)bytes.size()); }
    bool process(uint8_t &type, QByteArray &data);

private:
    static constexpr uint16_t MAX_DATA_SIZE = 128;
    static constexpr uint8_t  HEAD_SIZE     = 4;        // 帧头2 + type1 + len1
    static constexpr uint16_t FRAME_HEADER  = 0xA5B9;   // LTM 帧头（小端存储 B9 A5）

    int  headCheck(const uint8_t *head) const;          // 帧头 + 长度合法性 → 整帧长（含 CRC）

    RingBuffer m_rb;
};

QByteArray LtmProtocol::Private::package(uint8_t type, const QByteArray &data) const
{
    if (data.size() > (int)MAX_DATA_SIZE) return QByteArray();  // 超出 LTM 载荷上限

    QByteArray frame;
    frame.reserve(HEAD_SIZE + data.size() + 2);
    frame.append(char(0xB9));                                   // 0xA5B9 小端存储
    frame.append(char(0xA5));
    frame.append(char(type));
    frame.append(char(data.size()));
    frame.append(data);
    const uint16_t crc = crc16_modbus((const uint8_t *)frame.constData(), (uint16_t)frame.size());
    frame.append(char(crc & 0xFF));
    frame.append(char(crc >> 8));
    return frame;
}

bool LtmProtocol::Private::process(uint8_t &type, QByteArray &data)
{
    data.clear();
    uint8_t frame[HEAD_SIZE + MAX_DATA_SIZE + 2];
    while (m_rb.available() >= HEAD_SIZE) {
        uint8_t head[HEAD_SIZE];
        m_rb.peek(0, head, HEAD_SIZE);
        const int len = headCheck(head);
        if (len < 0) {
            m_rb.consume(1);                            // 帧头/长度不符：丢 1 字节重同步
            continue;
        }
        if (m_rb.available() < (uint16_t)len)
            return false;                               // 数据不足，等后续字节/分片

        m_rb.peek(0, frame, (uint16_t)len);
        uint16_t crcRecv;
        std::memcpy(&crcRecv, frame + len - 2, 2);
        if (crc16_modbus(frame, (uint16_t)(len - 2)) != crcRecv) {
            m_rb.consume(1);                            // CRC 不符：丢 1 字节重同步
            continue;
        }
        m_rb.consume((uint16_t)len);                    // 消费整帧
        type = frame[2];
        data = QByteArray((const char *)frame + HEAD_SIZE, frame[3]);
        return true;
    }
    return false;
}

int LtmProtocol::Private::headCheck(const uint8_t *head) const
{
    uint16_t header;
    std::memcpy(&header, head, 2);
    if (header != FRAME_HEADER)
        return -1;                                      // 前缀不符，重同步
    if (head[3] > MAX_DATA_SIZE)
        return -1;                                      // 数据长度非法
    return HEAD_SIZE + head[3] + 2;                     // 前缀 + 数据 + CRC2
}

/*==================== 公共 API（委托 Pimpl） ====================*/
LtmProtocol::LtmProtocol() : pimpl(new Private)            { }
LtmProtocol::~LtmProtocol()                                { delete pimpl; }
void LtmProtocol::init()                                   { pimpl->init(); }
QByteArray LtmProtocol::package(uint8_t type, const QByteArray &data) const { return pimpl->package(type, data); }
void LtmProtocol::receive(const QByteArray &bytes)         { pimpl->receive(bytes); }
bool LtmProtocol::process(uint8_t &type, QByteArray &data) { return pimpl->process(type, data); }
