#include "serial_protocol.h"
#include "serial_def.h"

// 构造函数
SerialProtocol::SerialProtocol() :buf_len(0) {
    rb_init();
}

// 初始化（可多次调用重置状态；LTM 与 Modbus 共用同一份环缓冲/输出缓冲）
void SerialProtocol::init() {
    rb_init();
    buf_len = 0;
    std::memset(send_buf, 0, sizeof(send_buf));         //重置发送缓冲区
}

// 协议解析：前缀判定注入 LTM 回调，取帧骨架共用
bool SerialProtocol::process(uint8_t &data_type, QByteArray& data) 
{
    data.clear();
    uint8_t frame[MAX_DATA_SIZE + 6];
    int len;
    if (!rb_take_frame(&SerialProtocol::ltmHeadCheck, nullptr, frame, &len))
        return false;
    data_type = frame[2];
    data = QByteArray((const char*)frame + HEAD_SIZE, len - HEAD_SIZE - 2);
    return true;
}

// 将待发送数据打包，用于串口发送（与 Modbus 同构：4字节前缀 + 数据 + CRC2）
void SerialProtocol::package(uint8_t data_type, const QByteArray& data) {
    int data_len = data.size();
    buf_len = 0;
    if(data_len > MAX_DATA_SIZE)   return;         // 可以发送空数据，比如启停命令
    // 4 字节前缀：帧头(2，小端) + 数据类型 + 数据长度
    uint16_t header = FRAME_HEADER;
    std::memcpy(send_buf, &header, sizeof(header));
    send_buf[2] = data_type;
    send_buf[3] = (uint8_t)data_len;
    if (data_len > 0)
        std::memcpy(send_buf + HEAD_SIZE, data.constData(), data_len);

    uint16_t crc = crc16_check(send_buf, data_len + HEAD_SIZE);
    std::memcpy(send_buf + HEAD_SIZE + data_len, &crc, sizeof(crc));
    buf_len = HEAD_SIZE + data_len + sizeof(crc);
}

// 返回打包后的原始数据
QByteArray SerialProtocol::getRawData() {
    return QByteArray((char *)send_buf, buf_len);
}

// 接收原始数据，写入环形缓冲区
void SerialProtocol::receive(const QByteArray& raw_data) {
    int data_len = raw_data.size();
    if (data_len == 0)    return;     // 判空
    // 写入环形缓冲区
    rb_write((const uint8_t*)raw_data.constData(), data_len);
}

// CRC-16 校验, Modbus RTU 校验算法
const uint16_t SerialProtocol::CRC16_TABLE[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

uint16_t SerialProtocol::crc16_check(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    const uint8_t* end = data + len;
    while (data < end) {
        crc = (crc >> 8) ^ CRC16_TABLE[(crc ^ *data++) & 0xFF];
    }

    return crc;
}

// 环形缓冲区初始化
void SerialProtocol::rb_init() {
    rb.head = 0;
    rb.tail = 0;
}

// 可读数据字节数
uint16_t SerialProtocol::rb_available() const {
    return (rb.head + RB_SIZE - rb.tail) % RB_SIZE;
}

// 剩余可写空间（保留1字节避免头尾重叠）
uint16_t SerialProtocol::rb_space() const {
    return (rb.tail + RB_SIZE - rb.head - 1) % RB_SIZE;
}

// 写入数据到环形缓冲区
bool SerialProtocol::rb_write(const uint8_t *data, uint16_t len) {
    if (rb_space() < len) return false;
    uint16_t head = rb.head;
    for (uint16_t i = 0; i < len; i++) {
        rb.buffer[head] = data[i];
        head = (head + 1) % RB_SIZE;
    }
    rb.head = head;
    return true;
}

// 偷看数据（不移动读指针）
bool SerialProtocol::rb_peek(uint16_t offset, uint8_t *data, uint16_t len) const {
    uint16_t avail = rb_available();
    if (avail < offset + len) return false;
    uint16_t pos = (rb.tail + offset) % RB_SIZE;
    for (uint16_t i = 0; i < len; i++) {
        data[i] = rb.buffer[pos];
        pos = (pos + 1) % RB_SIZE;
    }
    return true;
}

// 消费数据（移动读指针）
bool SerialProtocol::rb_consume(uint16_t len) {
    uint16_t avail = rb_available();
    if (avail < len) return false;
    rb.tail = (rb.tail + len) % RB_SIZE;
    return true;
}

// ==================== 通用帧解析骨架 ====================

// LTM 前缀判定：帧头合法 + 数据长度合法 → 整帧长（含 CRC）
int SerialProtocol::ltmHeadCheck(const uint8_t *head, const ModbusConfig *cfg)
{
    (void)cfg;
    uint16_t header;
    std::memcpy(&header, head, 2);
    if (header != FRAME_HEADER)             return -1;   // 前缀不符，重同步
    if (head[3] > MAX_DATA_SIZE)            return -1;   // 数据长度非法
    return HEAD_SIZE + head[3] + 2;                      // 前缀 + 数据 + CRC2
}

// Modbus 前缀判定：从站/功能码匹配 + 长度推导
// 读 0x01~0x04：5 + byteCount（前缀第 3 字节）；写 0x05/0x06/0x0F/0x10：8；异常：5
int SerialProtocol::modbusHeadCheck(const uint8_t *head, const ModbusConfig *cfg)
{
    if (head[0] != cfg->slave)              return -1;
    if (head[1] == (cfg->func | 0x80))      return 5;    // 异常帧
    if (head[1] != cfg->func)               return -1;
    switch (head[1]) {
    case 0x01: case 0x02:
    case 0x03: case 0x04:                   return 5 + head[2];  // 读：byteCount 就在前缀里
    case 0x05: case 0x06:
    case 0x0F: case 0x10:                   return 8;            // 写：回显请求
    default:                                return -1;
    }
}

// 通用取帧骨架：前缀判定 → 定长 → 整帧 CRC → 消费
// headCheck 返回：>0 整帧长度(含CRC)；0 数据不足继续等；-1 前缀不符（丢 1 字节重同步）
bool SerialProtocol::rb_take_frame(int (*headCheck)(const uint8_t *head, const ModbusConfig *cfg),
                                   const ModbusConfig *cfg,
                                   uint8_t *frame, int *frameLen)
{
    *frameLen = 0;
    while (rb_available() >= HEAD_SIZE) {
        uint8_t head[HEAD_SIZE];
        rb_peek(0, head, HEAD_SIZE);
        int len = headCheck(head, cfg);
        if (len < 0) {
            rb_consume(1);                  // 前缀不符，丢一字节重同步
            continue;
        }
        if (len == 0)   return false;       // 需要更多字节
        if (rb_available() < len)   return false;
        if (!rb_peek(0, frame, len))    break;
        uint16_t crc_recv;
        std::memcpy(&crc_recv, frame + len - 2, 2);
        if (crc16_check(frame, len - 2) != crc_recv) {
            rb_consume(1);                  // CRC 不符，丢一字节重同步
            continue;
        }
        rb_consume(len);                    // 消费一帧后立即返回
        *frameLen = len;
        return true;
    }
    return false;
}

// ==================== Modbus RTU ====================

// 组 Modbus 请求帧：slave + func + reg(2) + data + crc(2)
// 与 LTM 帧同构（4字节前缀 + 数据 + CRC2），统一走 send_buf，上限 MAX_DATA_SIZE；
// 实际写类请求仅几字节到几十字节，远小于上限。
void SerialProtocol::package(const ModbusConfig &cfg, const QByteArray &data)
{
    int data_len = data.size();
    buf_len = 0;
    if (data_len > MAX_DATA_SIZE)    return;   // 与 LTM 同一上限

    // 4 字节前缀：从站 + 功能码 + 起始寄存器（大端）
    send_buf[0] = cfg.slave;
    send_buf[1] = cfg.func;
    send_buf[2] = (uint8_t)(cfg.startReg >> 8);
    send_buf[3] = (uint8_t)(cfg.startReg & 0xFF);
    if (data_len > 0)
        std::memcpy(send_buf + HEAD_SIZE, data.constData(), data_len);

    uint16_t crc = crc16_check(send_buf, data_len + HEAD_SIZE);
    std::memcpy(send_buf + HEAD_SIZE + data_len, &crc, sizeof(crc));
    buf_len = HEAD_SIZE + data_len + sizeof(crc);
}

// 从环形缓冲区解析一帧 Modbus 响应（与 LTM 的 process 同构）
// 返回 true：正常响应 data=数据区；异常响应 cfg.func 置 |0x80，data=异常码
// 返回 false：未收齐或当前缓冲无有效帧
bool SerialProtocol::process(ModbusConfig &cfg, QByteArray &data)
{
    data.clear();
    uint8_t frame[256];
    int len;
    if (!rb_take_frame(&SerialProtocol::modbusHeadCheck, &cfg, frame, &len))
        return false;
    if (frame[1] == (cfg.func | 0x80))
        cfg.func |= 0x80;                   // 标记异常
    data = QByteArray((const char*)frame + 2, len - 4);   // 去掉从站+功能码+CRC（异常时即为异常码）
    return true;
}