#include "serial_protocol.h"
#include "serial_def.h"

// 数据类型名称映射表
static const char* data_type_names[Data_Unknown] = {
    "Data_Target",
    "Data_Position",
    "Data_Velocity",
    "Data_Current",
    "Data_Motor_Tempe",
    "Data_Driver_Tempe",
    "Data_CMD_Start",
    "Data_CMD_Reset",
    "Data_CMD_Set_PID",
    "Data_CMD_Set_Period",
    "Data_CMD_Stop",
    "Data_CMD_Jog",
    "Data_CMD_Forw",
    "Data_CMD_Reve",
    "Data_CMD_Text",              //文字指令，仅限控制台收发
    "Data_Ctrl_Pos",
    "Data_Ctrl_Vel",
    "Data_Ctrl_Tor",
    "Data_Ctrl_Open_Pos",
    "Data_Ctrl_Open_Vel",
    "Data_Ctrl_Sensorless_Vel",
    "Data_Ctrl_Stop_Quick",
    "Data_Res_Start",
    "Data_Res_Stop",
    "Data_Channel1",
    "Data_Channel2",
    "Data_Channel3",
    "Data_Channel4",
    "Data_Channel5",
	"Data_Channel_ALL",
	"Data_User_Defined",			/* 用户自定义通讯内容 */
};

// 构造函数
SerialProtocol::SerialProtocol() :buf_len(0) {
    rb_init();
}

// 初始化（可多次调用重置状态）
void SerialProtocol::init() {
    rb_init();
    buf_len = 0;
    rawData.clear();                                    //清空原始数据
    std::memset(send_buf, 0, sizeof(send_buf));         //重置发送缓冲区
}

// 协议解析
bool SerialProtocol::process(uint8_t &data_type, QByteArray& data) 
{
    return rb_parse_frame(&data_type, data);
}

// 将待发送数据打包成RawData格式，用于串口发送
void SerialProtocol::package(uint8_t data_type, const QByteArray& data) {
    int data_len = data.size();
    if(data_len > MAX_DATA_SIZE)   return;         //可以发送空数据，比如启停命令
    //帧头设置
    ProtocolHeader hdr;
    hdr.header = FRAME_HEADER;
    hdr.data_type = data_type;
    hdr.data_len = data_len;

    uint8_t* data_ptr = send_buf + sizeof(hdr);
    uint8_t* crc_ptr = data_ptr + data_len;

    std::memcpy(send_buf, &hdr, sizeof(hdr));
    std::memcpy(data_ptr, (uint8_t*)data.constData(), data_len);

    uint16_t crc = crc16_check(send_buf, data_len + sizeof(hdr));
    std::memcpy(crc_ptr, &crc, 2);
    buf_len = sizeof(hdr) + data_len + 2;
    //将缓冲区数据转换成原始数据
    rawData = QByteArray((char *)send_buf, buf_len);
}

// 返回打包后的原始数据
QByteArray SerialProtocol::getRawData() {
    return rawData;
}

// 接收原始数据，写入环形缓冲区
void SerialProtocol::receive(const QByteArray& raw_data) {
    int data_len = raw_data.size();
    if(data_len == 0)    return;     //判空
    //写入环形缓冲区
    rb_write((const uint8_t*)raw_data.constData(), data_len);
}

// CRC-16 校验, Modbus校验算法
uint16_t SerialProtocol::crc16_check(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

// 数据类型转字符串
const char* SerialProtocol::data_type_to_str(uint8_t type) {
    if (type < Data_Unknown)
        return data_type_names[type];
    else
        return "Unknown";
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

// 从环形缓冲区解析一帧
bool SerialProtocol::rb_parse_frame(uint8_t *data_type, QByteArray& data) {
    const uint8_t hdr_size = sizeof(ProtocolHeader);

    while (rb_available() >= 4) {
        uint16_t header;
        if (!rb_peek(0, (uint8_t*)&header, 2)) break;   // 检查帧头
        if (header != FRAME_HEADER) {
            rb_consume(1);
            continue;
        }
        if (rb_available() < sizeof(ProtocolHeader)) break;
        ProtocolHeader hdr;
        if (!rb_peek(0, (uint8_t*)&hdr, sizeof(hdr))) break;

        uint8_t hdr_data_len = hdr.data_len;
        if (hdr_data_len > MAX_DATA_SIZE) {           // 最大数据长度：128
            rb_consume(1);
            continue;
        }

        // 数据帧长度
        uint16_t total_len = sizeof(ProtocolHeader) + hdr_data_len + 2;
        if (total_len > RB_SIZE) {
            rb_consume(1);
            continue;
        }
        if (rb_available() < total_len) break;
        // 读取完整数据帧到缓冲区中
        uint8_t buffer[MAX_DATA_SIZE + 6];
        uint16_t crc_recv;
		if(!rb_peek(0, buffer, total_len)) break;
		std::memcpy(&crc_recv, buffer + hdr_size + hdr_data_len, 2);
		
		/* 开始 CRC 校验 */
		uint16_t calc_crc = crc16_check(buffer, hdr_size + hdr_data_len);
		if(calc_crc != crc_recv) {
			rb_consume(1);  		    // 丢弃一个字节，尝试重新同步 
			continue;           		// 继续while循环，寻找下一个帧头 
		}
		
		// 数据输出
		*data_type = hdr.data_type;
        data = QByteArray((const char*)(buffer + hdr_size), hdr_data_len);
		rb_consume(total_len);  	    // 消费整帧 
		
		return true;
    }
    return false;
}
