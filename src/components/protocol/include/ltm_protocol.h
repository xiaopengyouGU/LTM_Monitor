#ifndef LTM_PROTOCOL_H
#define LTM_PROTOCOL_H

#include <QByteArray>
#include <cstdint>

#if defined(PROTOCOL_LIBRARY)
#  define LTM_PROTOCOL_EXPORT Q_DECL_EXPORT
#else
#  define LTM_PROTOCOL_EXPORT Q_DECL_IMPORT
#endif

// LTM 协议支持的数据类型
typedef enum {
    Data_Target = 0,            // 目标值
    //指令, (上位机 --> 下位机)
    Data_CMD_Start,             // 启动
    Data_CMD_Stop,              // 停止
    Data_CMD_Reset,             // 系统软复位
    Data_CMD_Set_PID,           // 设置PID参数
    Data_CMD_Set_Period,        // 设置PID采样周期
    Data_CMD_Text,              // 文字指令，仅限控制台收发
    //通道数据，用于绘制曲线，周期发送（下位机 --> 上位机）
	Data_Channel_ALL,			// 所有通道数据一起发送，提高通讯效率
	Data_User_Defined,			// 用户自定义通讯内容
    Data_Unknown = 0xFF,
} DataTypes;

// 串口协议类型（上层选择通道解析模式）
typedef enum {
    Prot_LTM = 0,               // LTM 协议
    Prot_Common,                // 普通串口
    Prot_Modbus,                // Modbus RTU 协议
} ProtTypes;

// LTM-over-CANFD 协议 ID（与下位机约定一致）：
// 0x100 = 下行（上位机 → 电机广播）；0x101 = 上行（电机 → 上位机）
// 收发分 ID，避免总线上挂载多电机时重复触发/冲突
#define LTM_CANFD_DATA_ID       0x100  // 下行：LTM 帧载荷（载荷 = LTM 帧字节流）
#define LTM_CANFD_SEND_ID       0x101  // 上行：电机上报 LTM 帧（载荷 = LTM 帧字节流）

// LTM 协议解析
// 帧结构：0xA5B9(2, 小端) + type(1) + len(1) + data(len) + CRC16(2)
class LTM_PROTOCOL_EXPORT LtmProtocol
{
public:
    LtmProtocol();
    ~LtmProtocol();

    void init();                                        // 清空重组缓冲
    QByteArray package(uint8_t type, const QByteArray &data) const;   // 组 LTM 帧（>128B 返回空）
    void receive(const QByteArray &bytes);              // 字节流入环形缓冲（可跨帧 / 分片）
    bool process(uint8_t &type, QByteArray &data);      // 取一帧完整 LTM 帧（CRC 校验）

private:
    Q_DISABLE_COPY(LtmProtocol)
    class Private;
    Private *pimpl;
};

#endif // LTM_PROTOCOL_H
