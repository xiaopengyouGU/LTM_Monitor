#ifndef SERIAL_DEF_H
#define SERIAL_DEF_H

#include <cstdint>
#include <QMetaType>

typedef enum{
    //电机参数
    Data_Target = 0,
    Data_Position,
    Data_Velocity,
    Data_Current,
    Data_Motor_Tempe,
    Data_Driver_Tempe,
    //指令, (上位机 --> 下位机)
    Data_CMD_Start,             // 启动
    Data_CMD_Reset,             // 复位
    Data_CMD_Set_PID,           // 设置PID参数
    Data_CMD_Set_Period,        // 设置PID采样周期
    Data_CMD_Stop,              // 停机
    Data_CMD_Jog,               // 点动
    Data_CMD_Forw,              // 正转
    Data_CMD_Reve,              // 反转
    Data_CMD_Text,              // 文字指令，仅限控制台收发
    //控制模式
    Data_Ctrl_Pos,              // 位置控制
    Data_Ctrl_Vel,              // 速度控制
    Data_Ctrl_Tor,              // 力矩控制
    Data_Ctrl_Open_Pos,         // 开环位置
    Data_Ctrl_Open_Vel,         // 开环速度
    Data_Ctrl_Sensorless_Vel,   // 无感速度
    Data_Ctrl_Stop_Quick,       // 快速停机
    //响应，（下位机 --> 上位机）
    Data_Res_Start,             // 下位机启动
    Data_Res_Stop,              // 下位机停止
    //通道数据，用于绘制曲线，周期发送（下位机 --> 上位机）
    Data_Channel1,
    Data_Channel2,
    Data_Channel3,
    Data_Channel4,
    Data_Channel5,
	Data_Channel_ALL,			// 所有通道数据一起发送，提高通讯效率
	Data_User_Defined,			// 用户自定义通讯内容
    Data_Unknown = 0xFF,
}DataTypes;

typedef enum {                  // 串口协议类型 
    Prot_LTM = 0,               // LTM 协议
    Prot_Common,                // 普通串口
    Prot_Modbus,                // Modbus RTU 协议
}ProtTypes;

// Modbus RTU 轮询配置
struct ModbusConfig{
    uint8_t  slave = 1;          // 从站地址
    uint8_t  func  = 0x03;       // 功能码（默认读保持寄存器）
    uint16_t startReg = 0;       // 起始寄存器
    uint16_t quantity = 4;       // 寄存器数量
    int      intervalMs = 100;   // 轮询间隔
};

Q_DECLARE_METATYPE(ModbusConfig)

#endif // SERIAL_DEF_H
