#include "protocol/protocol.h"

#include <string.h>

typedef struct {
    uint8_t buffer[RB_SIZE];    /* 环形缓冲区 */
    uint16_t head;              /* 写指针，指向下一个写入的位置 */
    uint16_t tail;              /* 读指针，指向第一个可读数据 */
} ring_buffer_t;

/* 协议处理结构体 */
typedef struct {
    ring_buffer_t rb;           /* 环形缓冲区 */
    uint8_t send_buf[SEND_BUF_SIZE];  /* 待发送数据缓冲区 */
    uint8_t buf_len;            /* 发送缓冲区数据长度 */
    uint8_t flag;               /* 0 : 不显示数据 1 : 显示详细数据 */
} protocol_obj;

static protocol_obj _prot_obj;
static protocol_obj *prot = &_prot_obj;
static uint16_t crc16_check(const uint8_t* data, uint16_t len);
/* 环形缓冲区公共接口 */
static void rb_init(ring_buffer_t *rb);
static bool rb_write(ring_buffer_t *rb, const uint8_t *data, uint16_t len);
static bool rb_parse_frame(ring_buffer_t *rb, uint8_t *out_type, uint8_t *out_payload, uint16_t *out_payload_len);

/* 协议对象接口 */
void protocol_init(uint8_t flag)
{
    prot->flag = flag;
    rb_init(&prot->rb);
    prot->buf_len = 0;
    memset(prot->send_buf, 0, sizeof(prot->send_buf));
}

bool protocol_process(uint8_t *data_type, uint8_t *datas, uint16_t *data_len)
{
    /* BootLoader 版裁剪：flag 恒为 0，调试 printf 永不执行，删掉以省 printf 全家
     * （帧解析/打包逻辑与 LTM_HAL 原版完全一致，线格式不变） */
    return rb_parse_frame(&prot->rb, data_type, datas, data_len);
}

void protocol_package(uint8_t data_type, uint8_t *datas, uint16_t data_len)
{
    if (data_len > PROTOCOL_DATA_SIZE) return;      /* 数据过长，无法发送 */

    protocol_header hdr;
    hdr.header    = FRAME_HEADER;                   /* 帧头 */
    hdr.data_type = data_type;                      /* 数据类型 */
    hdr.data_len  = (uint8_t)data_len;              /* 数据长度 < 128 */
    uint16_t hdr_size = sizeof(hdr);

    /* 定位 CRC 位置 */
    uint8_t *send_buf = prot->send_buf;
    uint8_t *data_ptr = send_buf + hdr_size;        /* 数据起始位置 */
    uint8_t *crc_ptr  = data_ptr + data_len;        /* crc 起始位置 */

    memcpy(send_buf, &hdr,  hdr_size);
    memcpy(data_ptr, datas, data_len);
    /* 启动 CRC 校验 */
    uint16_t crc = crc16_check(send_buf, data_len + hdr_size);
    memcpy(crc_ptr, &crc, 2);
    /* 记录发送缓冲区数据长度 */
    prot->buf_len = hdr_size + data_len + sizeof(crc);
}

uint8_t* protocol_datas(uint16_t *len)
{
    if (!len) return NULL;                          /* 判空 */
    *len = prot->buf_len;
    return prot->send_buf;                          /* 返回缓冲 */
}

void protocol_recv(uint8_t *buf, uint16_t buf_len)  /* 将数据写入环形缓冲区 */
{
    rb_write(&prot->rb, buf, buf_len);
}

/************************************* 内部函数 ******************************************/
/* 经典 CRC 校验算法，与 Modbus RTU 相同 */
static uint16_t crc16_check(const uint8_t* data, uint16_t len)
{
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

static void rb_init(ring_buffer_t *rb)
{
    rb->head = rb->tail = 0;
}

/* 返回可读数据字节数 */
static uint16_t rb_available(ring_buffer_t *rb)
{
    return (rb->head + RB_SIZE - rb->tail) % RB_SIZE;
}

/* 返回剩余可写空间，不包括1个保留位 */
static uint16_t rb_space(ring_buffer_t *rb)
{
    return (rb->tail + RB_SIZE - rb->head - 1) % RB_SIZE;
}

/* 写入数据，成功返回true，空间不足返回false */
static bool rb_write(ring_buffer_t *rb, const uint8_t *data, uint16_t len)
{
    if (rb_space(rb) < len) return false;   /* 避免多次写入未读取而导致的数据覆盖 */

    uint16_t head = rb->head;
    for (uint16_t i = 0; i < len; i++) {
        rb->buffer[head] = data[i];
        head = (head + 1) % RB_SIZE;
    }
    rb->head = head;
    return true;
}

/* 偷看数据，从当前读指针偏移offset处读取len字节，不移动读指针 */
static bool rb_peek(ring_buffer_t *rb, uint16_t offset, uint8_t *data, uint16_t len)
{
    uint16_t avail = rb_available(rb);
    if (avail < offset + len) return false;

    uint16_t pos = (rb->tail + offset) % RB_SIZE;
    for (uint16_t i = 0; i < len; i++) {
        data[i] = rb->buffer[pos];
        pos = (pos + 1) % RB_SIZE;
    }
    return true;
}

/* 消费数据，向前移动读指针len字节 */
static bool rb_consume(ring_buffer_t *rb, uint16_t len)
{
    uint16_t avail = rb_available(rb);
    if (avail < len) return false;

    rb->tail = (rb->tail + len) % RB_SIZE;
    return true;
}

/* 环形缓冲区帧解析函数 */
static bool rb_parse_frame(ring_buffer_t *rb, uint8_t *data_type, uint8_t *data_buf, uint16_t *data_len)
{
    const uint16_t hdr_size = sizeof(protocol_header);

    while (rb_available(rb) >= hdr_size)    /* 至少要有帧头（2）+ 数据类型（1）+ 数据长度（1） */
    {
        uint16_t header;
        if (!rb_peek(rb, 0, (uint8_t*)&header, 2)) break;
        if (header != FRAME_HEADER) {
            rb_consume(rb, 1);              /* 不是帧头，丢弃 1 字节 */
            continue;
        }

        protocol_header hdr;                /* 获取帧结构体 */
        if (!rb_peek(rb, 0, (uint8_t*)&hdr, hdr_size)) break;
        uint8_t hdr_data_len = hdr.data_len;

        /* 验证接收数据的长度是否合法 */
        if (hdr_data_len > PROTOCOL_DATA_SIZE) {    /* 支持的最大数据：128 字节 */
            rb_consume(rb, 1);              /* 丢弃1字节 */
            continue;
        }

        uint16_t total_len = hdr_size + hdr_data_len + 2;   /* 接收到的帧长度 */
        if (total_len > RB_SIZE) {          /* 环形缓冲区大小：默认 256 字节 */
            rb_consume(rb, 1);              /* 丢弃1字节 */
            continue;
        }

        /* 检查是否有完整的一帧 */
        if (rb_available(rb) < total_len) break;

        /* 读取完整数据帧到缓冲区中，避免解析过程中发生数据覆盖 */
        uint8_t buffer[RECV_BUF_SIZE];
        uint16_t crc_recv;
        if (!rb_peek(rb, 0, buffer, total_len)) break;
        memcpy(&crc_recv, buffer + hdr_size + hdr_data_len, 2);

        /* 开始 CRC 校验 */
        uint16_t calc_crc = crc16_check(buffer, hdr_size + hdr_data_len);
        if (calc_crc != crc_recv) {
            rb_consume(rb, 1);              /* 丢弃一个字节，尝试重新同步 */
            continue;                       /* 继续while循环，寻找下一个帧头 */
        }

        /* 数据输出 */
        *data_type = hdr.data_type;
        *data_len  = hdr_data_len;
        memcpy(data_buf, buffer + hdr_size, hdr_data_len);
        rb_consume(rb, total_len);          /* 消费整帧 */

        return true;
    }
    return false;
}
