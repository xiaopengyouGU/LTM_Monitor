#include "protocol.h"

static protocol_obj _prot_obj;
static protocol_obj *prot = &_prot_obj;

static const char* data_type_to_str(uint8_t type);
static uint16_t crc16_check(const uint8_t* data, uint16_t len);
static bool parse_frame(const uint8_t* buffer, uint16_t recv_len, uint8_t* out_data_type,
                 uint8_t* out_payload, uint16_t* out_payload_len, uint16_t max_payload_size);
//环形缓冲区公共接口
static void rb_init(ring_buffer_t *rb);
static bool rb_write(ring_buffer_t *rb, const uint8_t *data, uint16_t len);
static bool rb_parse_frame(ring_buffer_t *rb, uint8_t *out_type, uint8_t *out_payload, uint16_t *out_payload_len);

//协议对象接口                 
void protocol_init(uint8_t flag)
{
    prot->flag = flag;
    rb_init(&prot->rb);
    prot->buf_len = 0;
    memset(prot->send_buf, 0, sizeof(prot->send_buf));
}

bool protocol_process(uint8_t *data_type, uint8_t *datas, uint16_t *data_len)    //协议解析
{
    bool res = rb_parse_frame(&prot->rb, data_type, datas, data_len);

    if(res)
    {
        if(prot->flag != 0)
        {
            printf("\nprocess protocol success!!! \n");
            printf("data len = %d, \t data type : %s (0x%02X)\n", *data_len, data_type_to_str(*data_type),*data_type);
            printf("recv datas : \n");
            for(uint16_t i = 0; i < *data_len; i++)
            {
                printf("0x%02x \t",datas[i]);
            }
            printf("\n");
        }
    }
    else
    {
        if(prot->flag != 0) 
            printf("process protocol failed! \n");
    }
    return res;
}

void protocol_package(uint8_t data_type, uint8_t *datas, uint16_t data_len)    //数据打包
{
    protocol_header hdr;             //帧头
    hdr.header = FRAME_HEADER;
    hdr.data_type = data_type;
    hdr.data_len = data_len;
    //定位CRC和帧尾位置
    uint8_t* data_ptr = prot->send_buf + sizeof(hdr);  //数据起始位置
    uint8_t* crc_ptr = data_ptr + data_len;            //crc起始位置
    uint8_t* tail_ptr = crc_ptr + 2;
    
    memcpy(prot->send_buf, &hdr, sizeof(hdr));
    memcpy(data_ptr, datas, data_len);

    uint16_t crc = crc16_check(prot->send_buf, data_len + sizeof(hdr));
    uint32_t tail = FRAME_TAILER;
    memcpy(crc_ptr, &crc, 2);
    memcpy(tail_ptr, &tail, 4);
    //记录缓冲区数据长度
    prot->buf_len = sizeof(hdr) + data_len + 2 + 4;

    //打印打包好的数据
    if(prot->flag != 0)
    {
        printf("send buf (len = %d) : \n", prot->buf_len);
        for(uint16_t i = 0; i < prot->buf_len; i++)
        {
            printf("0x%02x ",prot->send_buf[i]);
        }
        printf("\n");
        printf("data len = %d, \t data type : %s (0x%02X)\n",data_len,data_type_to_str(data_type),data_type);
        printf("send datas : \n");
        for(uint16_t i = 0; i < data_len; i++)
        {
            printf("0x%02x \t", datas[i]);
        }
        printf("\n");
    }
}

uint8_t* protocol_datas(uint16_t *len)                                //获取发送缓冲区数据
{
    *len = prot->buf_len;
    return prot->send_buf;              //返回缓冲区
}

void protocol_recv(uint8_t *buf, uint16_t buf_len)
{
    rb_write(&prot->rb, buf, buf_len);
}

/************************************* 内部函数 ******************************************/
//经典CRC校验算法
static uint16_t crc16_check(const uint8_t* data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) 
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) 
        {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static void rb_init(ring_buffer_t *rb)
{
    rb->head = rb->tail = 0;
}

//返回可读数据字节数
static uint16_t rb_available(ring_buffer_t *rb)
{
    return (rb->head + RB_SIZE - rb->tail) % RB_SIZE;
}

//返回剩余可写空间，不包括1个保留位
static uint16_t rb_space(ring_buffer_t *rb)
{
    return (rb->tail + RB_SIZE - rb->head - 1) % RB_SIZE;
}

//写入数据，成功返回true,空间不足返回false
static bool rb_write(ring_buffer_t *rb, const uint8_t *data, uint16_t len)
{
    if(rb_space(rb) < len) return false;   //避免多次写入未读取而导致的数据覆盖

    uint16_t head = rb->head;
    for(uint16_t i = 0; i < len; i++)
    {
        rb->buffer[head] = data[i];
        head = (head + 1) % RB_SIZE;        //环形缓冲区
    }
    rb->head = head;
    return true;
}

//偷看数据，从当前读指针偏移offset处读取len字节，不移动读指针
static bool rb_peek(ring_buffer_t *rb, uint16_t offset, uint8_t *data, uint16_t len)
{
    uint16_t avail = rb_available(rb);
    if(avail < offset + len) return false;

    uint16_t pos = (rb->tail + offset) % RB_SIZE;
    for(uint16_t i = 0; i < len; i++)
    {
        data[i] = rb->buffer[pos];
        pos = (pos + 1) % RB_SIZE; 
    }
    return true;
}

//消费数据，向前移动读指针len字节
static bool rb_consume(ring_buffer_t *rb, uint16_t len)
{
    uint16_t avail = rb_available(rb);
    if(avail < len) return false;

    rb->tail = (rb->tail + len) % RB_SIZE;
    return true;
}


//环形缓冲区帧解析函数
static bool rb_parse_frame(ring_buffer_t *rb, uint8_t *out_type, uint8_t *out_payload, uint16_t *out_payload_len)
{
    while (rb_available(rb) >= 4)    //至少要有帧头长度
    {
        uint32_t header;
        if(!rb_peek(rb, 0, (uint8_t*)&header, 4)) break;
        if(header != FRAME_HEADER)
        {
            rb_consume(rb, 1);      //不是帧头，丢弃1字节
            continue;
        }
        
        //检查是否能读取完整头部（7字节）
        if(rb_available(rb) < sizeof(protocol_header)) break;

        protocol_header hdr;
        if(!rb_peek(rb, 0, (uint8_t*)&hdr, sizeof(hdr))) break;

        //验证数据长度合法
        if(hdr.data_len > 64)
        {
            rb_consume(rb, 1);      //丢弃1字节
            continue;
        }

        uint16_t total_len = sizeof(protocol_header) + hdr.data_len + 2 + 4;
        if(total_len > RB_SIZE)     //检查帧长度
        {
            rb_consume(rb, 1);      //丢弃1字节
            continue;
        }   

        //检查是否有完整的一帧
        if(rb_available(rb) < total_len) break;

        //读取完整的帧到临时缓冲区
        uint8_t frame[256];
        if(!rb_peek(rb, 0, frame, total_len)) break;

        //调用parse_frame验证
        if(parse_frame(frame, total_len, out_type, out_payload, out_payload_len, 64))
        {
            rb_consume(rb, total_len);  //消费该帧
            return true;
        }
        else
        {   
            rb_consume(rb, 1);          //校验失败，丢弃一个字节继续
            continue;
        }
    }
    return false;
}

/**
 * 解析一帧数据（已从数据流中完整提取）
 * @param buffer         完整帧数据缓冲区
 * @param recv_len       缓冲区长度（字节）
 * @param out_data_type  输出：数据类型
 * @param out_payload    输出：数据内容缓冲区（调用者提供）
 * @param out_payload_len 输出：实际数据长度
 * @param max_payload_size out_payload 的最大容量
 * @return true 解析成功，false 解析失败
 */
//帧处理接口
static bool parse_frame(const uint8_t* buffer, uint16_t recv_len, uint8_t* out_data_type,
                 uint8_t* out_payload, uint16_t* out_payload_len, uint16_t max_payload_size)
{
    //最小帧长度：头部（7）+ 无数据 + CRC（2）+ 帧尾（4） = 13
    if(recv_len < 13) return false;

    // 1. 解析头部，并验证帧头
    protocol_header hdr;
    memcpy(&hdr, buffer, sizeof(hdr));
    if(hdr.header != FRAME_HEADER) return false;

    // 2. 验证数据长度合法
    uint16_t data_len = hdr.data_len; //小端
    if(data_len > 64) return false;

    //计算理论帧长度：头 + 数据 + CRC(2) + 帧尾（4）
    uint16_t expected_len = sizeof(protocol_header) + data_len + 2 + 4;
    if(recv_len < expected_len) return false;   //缓冲区数据不足一帧

    // 3. 定位CRC和帧尾位置
    const uint8_t* data_ptr = buffer + sizeof(protocol_header);  //数据起始位置
    const uint8_t* crc_ptr = data_ptr + data_len;       //crc起始位置
    const uint8_t* tail_ptr = crc_ptr + 2;

    // 4. 验证帧尾
    uint32_t tail;
    memcpy(&tail, tail_ptr, 4);
    if(tail != FRAME_TAILER) return false;

    // 5. 验证CRC
    uint16_t calc_crc = crc16_check(buffer, sizeof(protocol_header) + data_len);
    uint16_t recv_crc;
    memcpy(&recv_crc, crc_ptr, 2);
    if(recv_crc != calc_crc) return false;

    // 6. 输出数据
    if(data_len > max_payload_size) return false;   //输出缓冲区不足
    memcpy(out_payload, data_ptr, data_len);
    *out_payload_len = data_len;
    *out_data_type = hdr.data_type;

    return true;
}

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
    "Data_Channel5"
};

static const char* data_type_to_str(uint8_t type) 
{
    if (type < sizeof(data_type_names) / sizeof(data_type_names[0])) 
    {
        return data_type_names[type];
    } 
    else    return "Unknown";
}