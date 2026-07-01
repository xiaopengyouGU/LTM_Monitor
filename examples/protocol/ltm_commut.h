#ifndef __LTM_COMMUT_H__
#define __LTM_COMMUT_H__

#include "protocol/protocol.h"
/* LTM 通讯应用层接口, 用户只需实现 ltm_commut.c 文件 */

typedef struct{
    const struct ltm_commut_ops *ops;
    uint8_t init;               /* 初始化标志，0：未初始化，1：初始化完毕 */
    uint8_t rx_buf[128];        /* 接收缓冲区，单次最多 128 字节数据 */
    uint8_t data_type;          /* 接收到的数据类型 */
    uint16_t rx_len;            /* 接收到的数据长度 */
    void (*send)(uint8_t *datas, uint16_t len);  /* 底层发送接口 */
} ltm_commut;

typedef struct{
	float values[5];			/* 实际值数组 */
	uint8_t size;				/* 发送的曲线数量 */
} ltm_curves;

/* 用户API */
void ltm_commut_init(void);                     /* 初始化通讯模块 */
void ltm_commut_set_send(void (*send)(uint8_t *datas, uint16_t len));	/* 设置底层发送接口 */
void ltm_commut_recv(uint8_t *datas, uint16_t len);						/* 数据接收接口：封装 protocol_recv */
void ltm_commut_send(uint8_t data_type, uint8_t *datas, uint16_t len);	/* datas: 待发送的原始数据，len: 原始数据长度 */
void ltm_commut_printf(const char *fmt, ...);							/* 字符串格式化输出 */
void ltm_commut_send_curve(uint8_t ch, float value);					/* 发送单条曲线: 对应实际值 */
void ltm_commut_send_curves(ltm_curves * curves);						/* 发送多条曲线（目标值和实际值均可） */
bool ltm_commut_process(uint8_t *data_type, uint8_t* datas, uint16_t *len); /* 通讯协议解析，必须周期调用 */        					

#endif