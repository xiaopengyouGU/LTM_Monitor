#ifndef CANFD_DEF_H
#define CANFD_DEF_H

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <cstdint>

#if defined(CANFD_LIBRARY)
#  define CANFD_EXPORT Q_DECL_EXPORT
#else
#  define CANFD_EXPORT Q_DECL_IMPORT
#endif

#define MIN_POLL_TIME           2     // 最小轮询时间 2ms 


// CAN ID 标志位（与厂商 SDK 保持一致）
#define CANFD_EFF_FLAG  0x80000000U   // 扩展帧标志
#define CANFD_RTR_FLAG  0x40000000U   // 远程帧标志
#define CANFD_ERR_FLAG  0x20000000U   // 错误帧标志
#define CANFD_ID_MASK   0x1FFFFFFFU   // 29 位 ID 掩码

#define CANFD_MAKE_ID(id, eff, rtr, err) ((id) | (!!(eff) << 31) | (!!(rtr) << 30) | (!!(err) << 29))
#define CANFD_IS_EFF(id) (!!((id) & CANFD_EFF_FLAG))
#define CANFD_IS_RTR(id) (!!((id) & CANFD_RTR_FLAG))
#define CANFD_GET_ID(id) ((id) & CANFD_ID_MASK)

// 设备类型（ZCAN_USBCANFD_200U）
enum CanfdDeviceType { CANFD_DEVICE_USBCANFD_200U = 41 };

// 厂商类型（只决定首选驱动 DLL，设备类型映射与厂商无关）
enum CanfdVendor { CANFD_VENDOR_CHUANGXIN = 0, CANFD_VENDOR_ZQWL = 1 };

// 协议类型
enum CanfdProtocol { CANFD_PROTOCOL_CAN = 0, CANFD_PROTOCOL_CANFD = 1 };

// 帧格式
enum CanfdFrameFormat { CANFD_FRAME_STANDARD = 0, CANFD_FRAME_EXTENDED = 1 };

// CANFD 标准
enum CanfdStandard { CANFD_STANDARD_ISO = 0, CANFD_STANDARD_BOSCH = 1 };

// CAN/CAN-FD 帧（收发共用）
struct CanfdFrame
{
    uint32_t   id = 0;           // 32 位完整 ID（含 EFF/RTR 标志位）
    uint8_t    len = 0;          // 数据长度 0~64
    uint8_t    flags = 0;        // FD 帧标志（bit0 = BRS 加速），经典 CAN 填 0
    uint8_t    channel = 0;      // 通道号 0/1
    bool       isFd = false;     // true：CAN-FD 帧，false：经典 CAN 帧
    uint64_t   timestampUs = 0;      // 设备时间戳（us），发送时填 0
    qint64     timestampEpochMs = 0; // 设备 us 换算后的主机 epoch 时间（ms），发送时填 0
    uint8_t    transmitType = 0;     // 发送方式：0=正常发送，2=自发自收（接收帧填 0）
    QByteArray data;             // 有效数据 len 字节

    bool isEff() const { return CANFD_IS_EFF(id); }
    bool isRtr() const { return CANFD_IS_RTR(id); }
    uint32_t rawId() const { return CANFD_GET_ID(id); }
    bool brs() const { return (flags & 0x01U) != 0; }
};

// 通道初始化配置
struct CanfdConfig
{
    uint32_t deviceIndex = 0;                      // 设备序号，默认 0
    int      vendor = CANFD_VENDOR_CHUANGXIN;      // 厂商（优先使用对应的驱动 DLL）
    QString  driverPath;                          // 可选：显式指定驱动 DLL 路径
    int      deviceType = CANFD_DEVICE_USBCANFD_200U;
    int      channels = 2;                         // 使用的通道数：1 或 2
    uint32_t abitBaud = 500000;                    // 仲裁段波特率
    uint32_t dbitBaud = 2000000;                   // 数据段波特率（仅 CAN-FD）
    int      canfdStandard = CANFD_STANDARD_ISO;   // 0：ISO，1：BOSCH
    bool     resistance[2] = {true, true};         // 终端电阻（每通道）
    bool     customBaud = false;                   // true：使用自定义波特率
    QString  customBaudValue;                      // 自定义波特率字符串，如 "1M,5M"
    uint8_t  filterMode = 0;                       // 0：不过滤，1：标准帧，2：扩展帧，3：全部
    uint32_t filterStartId = 0;                    // 滤波起始 ID
    uint32_t filterEndId = 0x1FFFFFFFU;            // 滤波结束 ID
    int      pollIntervalMs = 20;                  // 接收轮询周期（ms）
};

Q_DECLARE_METATYPE(CanfdFrame)
Q_DECLARE_METATYPE(CanfdConfig)

#endif // CANFD_DEF_H
