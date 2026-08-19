#ifndef __LTM_COMMUT_H__
#define __LTM_COMMUT_H__

#include <stdint.h>
#include <stdbool.h>
/* LTM 通讯应用层接口 */

#define LTM_CURVE_SIZE			5	/* 支持发送的曲线数 */

typedef enum {                  /* LTM 通讯协议 支持的数据类型 */
    Data_Target = 0,            /* 目标值 */
    Data_CMD_Start,             /* 启动 */
    Data_CMD_Stop,              /* 停止 */
    Data_CMD_Reset,             /* 系统软复位 */
    Data_CMD_Set_PID,           /* 设置PID参数 */
    Data_CMD_Set_Period,        /* 设置PID采样周期 */
    Data_CMD_Text,              /* 文本指令，仅限控制台收发 */
    /* 通道数据，用于绘制曲线，周期发送（下位机 --> 上位机） */
    Data_Channel_ALL,           /* 所有通道数据一起发送，提高通讯效率 */
    Data_User_Defined,          /* 用户自定义通讯内容（IAP 命令走此通道） */
    Data_Unknown = 0xFF,        /* 未知数据 */
} DataTypes;

typedef struct{
	float values[LTM_CURVE_SIZE];	/* 实际值数组 */
	uint8_t size;					/* 发送的曲线数量 */
} ltm_curves;

/* 用户API */
void ltm_commut_init(void);                     /* 初始化通讯模块 */
void ltm_commut_set_send(void (*send)(uint8_t *datas, uint16_t len));	/* 设置底层发送接口 */
void ltm_commut_recv(uint8_t *datas, uint16_t len);						/* 数据接收接口：封装 protocol_recv */
void ltm_commut_send(uint8_t data_type, uint8_t *datas, uint16_t len);	/* datas: 待发送的原始数据，len: 原始数据长度 */
void ltm_commut_printf(const char *fmt, ...);							/* 字符串格式化输出 */
void ltm_commut_send_curves(ltm_curves * curves);						/* 发送多条曲线（目标值和实际值均可） */
bool ltm_commut_process(uint8_t *data_type, uint8_t* datas, uint16_t *len); /* 通讯协议解析，必须周期调用 */        					

#endif