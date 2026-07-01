#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include<stdint.h>
#include<stdbool.h>
#include<string.h>
#include<stdio.h>

typedef enum{
    //电机参数
    Data_Target = 0,
    Data_Position,
    Data_Velocity,
    Data_Current,
    Data_Motor_Tempe,
    Data_Driver_Tempe,
    //指令, (上位机 --> 下位机)
    Data_CMD_Start,             //启动
    Data_CMD_Reset,             //复位
    Data_CMD_Set_PID,           //设置PID参数
    Data_CMD_Set_Period,        //设置PID采样周期
    Data_CMD_Stop,              //停机
    Data_CMD_Jog,               //点动
    Data_CMD_Forw,              //正转
    Data_CMD_Reve,              //反转
    Data_CMD_Text,              //文字指令，仅限控制台收发
    //控制模式
    Data_Ctrl_Pos,              //位置控制
    Data_Ctrl_Vel,              //速度控制
    Data_Ctrl_Tor,              //力矩控制
    Data_Ctrl_Open_Pos,         //开环位置
    Data_Ctrl_Open_Vel,         //开环速度
    Data_Ctrl_Sensorless_Vel,   //无感速度
    Data_Ctrl_Stop_Quick,       //快速停机
    //响应，（下位机 --> 上位机）
    Data_Res_Start,             //下位机启动
    Data_Res_Stop,              //下位机停止
    //通道数据，用于绘制曲线，周期发送（下位机 --> 上位机）
    Data_Channel1,
    Data_Channel2,
    Data_Channel3,
    Data_Channel4,
    Data_Channel5,
	Data_Channel_ALL,			//所有通道数据一起发送，提高通讯效率
	Data_User_Defined,			//用户自定义通讯内容
    Data_Unknown,
}DataTypes;

/* 通讯协议，默认运行在小端序平台。
 * 数据帧结构：帧头（2）+ 数据类型（1）+数据长度（1）+ 数据（N）+ CRC校验位（2）
 * 固定帧长度：6字节
 */
#pragma pack(push, 1)
typedef struct{
    uint16_t header;    /* 帧头：0xA5B9 */
    uint8_t data_type;  /* 数据类型 */
    uint8_t data_len;	/* 数据长度：<= 128 字节 */
}protocol_header;
#pragma pack(pop)

//环形缓冲区定义与接口
#define RB_SIZE 256  //环形缓冲区大小
#define PROTOCOL_DATA_SIZE			128				/* 最长数据：128字节 */
#define FRAME_HEADER   0xA5B9   /* 小端存储（2字节）：B9 A5，极其冷门 */	
/* 数据帧已经带校验位和数据长度，帧尾没有必要存在 */

typedef struct {
    uint8_t buffer[RB_SIZE];    //环形缓冲区
    uint16_t head;              //写指针，指向下一个写入的位置
    uint16_t tail;              //读指针，指向第一个可读数据
}ring_buffer_t;

//协议处理结构体
typedef struct {
    ring_buffer_t rb;           //环形缓冲区
    uint8_t send_buf[RB_SIZE];  //待发送数据缓冲区
    uint8_t buf_len;            //缓冲区数据长度
    uint8_t flag;               // 0 : 不显示数据, 1 : 显示详细数据
}protocol_obj;

void protocol_init(uint8_t flag);
bool protocol_process(uint8_t *data_type, uint8_t *datas, uint16_t *data_len);  //协议解析
void protocol_package(uint8_t data_type, uint8_t *datas, uint16_t data_len);    //数据打包
uint8_t* protocol_datas(uint16_t *buf_len);                                     //获取发送缓冲区数据
void protocol_recv(uint8_t *buf, uint16_t buf_len);                             //该函数直接写入环形缓冲区



#endif