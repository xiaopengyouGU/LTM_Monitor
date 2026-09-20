#include "modbus_protocol.h"
#include "ring_buffer.h"
#include <cstring>

class ModbusProtocol::Private
{
public:
    void init() { m_rb.reset(); }
    QByteArray package(const ModbusConfig &cfg, const QByteArray &data) const;
    void receive(const QByteArray &bytes) { m_rb.write((const uint8_t *)bytes.constData(), (uint16_t)bytes.size()); }
    bool process(ModbusConfig &cfg, QByteArray &data);

private:
    static constexpr uint16_t MAX_DATA_SIZE = 250;
    static constexpr uint8_t  HEAD_SIZE     = 4;        // slave + func + reg(2)
    static int  headCheck(const uint8_t *head, const ModbusConfig *cfg);

    RingBuffer m_rb;
};

QByteArray ModbusProtocol::Private::package(const ModbusConfig &cfg, const QByteArray &data) const
{
    if (data.size() > (int)MAX_DATA_SIZE) return QByteArray();

    QByteArray frame;
    const uint8_t func = cfg.func;
    // ??0x01~0x04??slave + func + startReg(2) + quantity(2) + CRC
    // ????0x05/0x06??slave + func + addr(2) + value(2) + CRC
    // ????0x0F/0x10??slave + func + startReg(2) + quantity(2) + byteCount(1) + data(N) + CRC
    if (func == 0x05 || func == 0x06) {
        if (data.size() != 2) return QByteArray();
    }
    frame.append(char(cfg.slave));
    frame.append(char(cfg.func));
    frame.append(char(cfg.startReg >> 8));
    frame.append(char(cfg.startReg & 0xFF));
    if (func >= 0x01 && func <= 0x04) {
        frame.append(char(cfg.quantity >> 8));
        frame.append(char(cfg.quantity & 0xFF));
    } else if (func == 0x05 || func == 0x06) {
        frame.append(data);                         // ??????? 2 ???
    } else if (func == 0x0F || func == 0x10) {
        frame.append(char(cfg.quantity >> 8));
        frame.append(char(cfg.quantity & 0xFF));
        frame.append(char(data.size()));            // ???
        frame.append(data);
    } else {
        return QByteArray();                        // ???????
    }

    const uint16_t crc = crc16_modbus((const uint8_t *)frame.constData(), (uint16_t)frame.size());
    frame.append(char(crc & 0xFF));
    frame.append(char(crc >> 8));
    return frame;
}

bool ModbusProtocol::Private::process(ModbusConfig &cfg, QByteArray &data)
{
    data.clear();
    cfg.isException = false;                        // ??????????????
    uint8_t frame[260];                             // Modbus 读响应最大 5+253=258B，留余量
    while (m_rb.available() >= HEAD_SIZE) {
        uint8_t head[HEAD_SIZE];
        m_rb.peek(0, head, HEAD_SIZE);
        const int len = headCheck(head, &cfg);      
        if (len < 0) {
            m_rb.consume(1);                        // 前缀不符，丢 1 字节重同步
            continue;
        }
        if (m_rb.available() < (uint16_t)len)
            return false;                           // 数据不足
        m_rb.peek(0, frame, (uint16_t)len);
        uint16_t crcRecv;
        std::memcpy(&crcRecv, frame + len - 2, 2);
        if (crc16_modbus(frame, (uint16_t)(len - 2)) != crcRecv) {
            m_rb.consume(1);                        // CRC 不符，丢 1 字节重同步
            continue;
        }
        m_rb.consume((uint16_t)len);

        // 检查是否是异常响应
        if (frame[1] & 0x80) {
            cfg.isException = true;                         // 标记异常
            data = QByteArray((const char *)frame + 2, 1);  // 异常码
            return true;
        }
        // 正常响应：根据功能码提取数据或回填信息
        const uint8_t func = frame[1];
        if (func >= 0x01 && func <= 0x04) {
            // 读命令的响应：从站(1) | 功能码(1) | 字节数(1) | 数据(N) | CRC(2)
            // 只回填数据，地址和数量保持请求时的值不变
            data = QByteArray((const char *)frame + 3, (int)(frame[2]));
        } else if (func == 0x05 || func == 0x06 || func == 0x0F || func == 0x10) {
            // 写命令的响应：从站(1) | 功能码(1) | 起始地址(2) | 数量(2) | CRC(2)
            // 回填起始地址和数量，data 留空
            cfg.startReg = (uint16_t)((frame[2] << 8) | frame[3]);
            cfg.quantity = (uint16_t)((frame[4] << 8) | frame[5]);
        } else {    // 未知功能码，按原逻辑处理（保守）
            data = QByteArray((const char *)frame + 2, len - 4);
        }
        return true;
    }
    return false;
}

// 从站/功能码匹配 + 长度推导：读 0x01~0x04：5+byteCount；写 0x05/0x06/0x0F/0x10：8；异常：5
int ModbusProtocol::Private::headCheck(const uint8_t *head, const ModbusConfig *cfg)
{
    if (head[0] != cfg->slave)
        return -1;
    if (head[1] == (cfg->func | 0x80))
        return 5;                                   // 异常帧
    if (head[1] != cfg->func)
        return -1;
    switch (head[1]) {
    case 0x01: case 0x02:
    case 0x03: case 0x04:
        if (head[2] > MAX_DATA_SIZE)  return -1;    // 读：byteCount 超上限（125 寄存器 = 250B）
        return 5 + head[2];
    case 0x05: case 0x06:
    case 0x0F: case 0x10:   return 8;               // 写：回显请求
    default:                return -1;
    }
}

/*==================== 公共 API（委托 Pimpl） ====================*/
ModbusProtocol::ModbusProtocol() : pimpl(new Private)               { }
ModbusProtocol::~ModbusProtocol()                                   { delete pimpl; }
void ModbusProtocol::init()                                         { pimpl->init(); }
QByteArray ModbusProtocol::package(const ModbusConfig &cfg, const QByteArray &data) const { return pimpl->package(cfg, data); }
void ModbusProtocol::receive(const QByteArray &bytes)               { pimpl->receive(bytes); }
bool ModbusProtocol::process(ModbusConfig &cfg, QByteArray &data)   { return pimpl->process(cfg, data); }
