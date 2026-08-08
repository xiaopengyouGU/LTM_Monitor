#include "canfd_controller.h"
#include "ControlCANFD.h"        // 厂商 SDK：third_party/zcan/include
#include <QDateTime>

// 厂商 ControlCANFD.h 未声明该接口，但 DLL 已导出；官方 zlgcan.h 中的定义如下：
#ifndef ZCAN_CHANNEL_ERR_INFO
typedef struct tagZCAN_CHANNEL_ERR_INFO {
    UINT error_code;
    BYTE passive_ErrData[3];
    BYTE arLost_ErrData;
} ZCAN_CHANNEL_ERR_INFO;
#endif

extern "C" {
UINT FUNC_CALL ZCAN_ReadChannelErrInfo(CHANNEL_HANDLE channel_handle, ZCAN_CHANNEL_ERR_INFO *p_err_info);
}
#include <cstring>

namespace
{
const int kMaxBatch = 512;       // 单次读取最大帧数
}

class CanfdController::Private
{
public:
    CanfdConfig    config;
    int            channelCount = 0;
    DEVICE_HANDLE  dev = INVALID_DEVICE_HANDLE;
    CHANNEL_HANDLE ch[2] = {INVALID_CHANNEL_HANDLE, INVALID_CHANNEL_HANDLE};
    IProperty     *prop = nullptr;
    bool           open = false;
    QString        lastError;
    qint64         epochOffsetMs = 0;   // 设备时间 -> 主机 epoch 的偏移
    bool           epochCalibrated = false;
    uint32_t       rxTotal = 0;         // 累计接收帧数
    uint32_t       txTotal = 0;         // 累计发送帧数

    // 属性配置：CANFD 标准 / 波特率 / 终端电阻 / 滤波（照搬厂商 Qt 示例的属性路径）
    bool applyPropertyConfig() {
        for (int i = 0; i < channelCount; i++) {
            QString p = QString::number(i);
            if (!setProp(p + QString("/canfd_standard"), QString::number(config.canfdStandard)))
                return false;
            if (config.customBaud) {
                if (!setProp(p + QString("/baud_rate_custom"), config.customBaudValue))
                    return false;
            } else {
                if (!setProp(p + QString("/canfd_abit_baud_rate"), QString::number(config.abitBaud)))
                    return false;
                if (!setProp(p + QString("/canfd_dbit_baud_rate"), QString::number(config.dbitBaud)))
                    return false;
            }
            if (!setProp(p + QString("/initenal_resistance"), config.resistance[i] ? QString("1") : QString("0")))
                return false;
        }

        if (config.filterMode != 0) {
            for (int i = 0; i < channelCount; i++) {
                QString p = QString::number(i);
                if (!setProp(p + QString("/filter_clear"), QString("0")))
                    return false;
                if (!setProp(p + QString("/filter_mode"), QString::number(config.filterMode)))
                    return false;
                if (!setProp(p + QString("/filter_start"), QString("0x") + QString::number(config.filterStartId, 16)))
                    return false;
                if (!setProp(p + QString("/filter_end"), QString("0x") + QString::number(config.filterEndId, 16)))
                    return false;
                if (!setProp(p + QString("/filter_ack"), QString("0")))
                    return false;
            }
        }
        return true;
    }

    bool setProp(const QString &path, const QString &value) {
        if (!prop || !prop->SetValue) {
            lastError = QString("IProperty 不可用");
            return false;
        }
        QByteArray p = path.toUtf8();
        QByteArray v = value.toUtf8();
        if (prop->SetValue(p.constData(), v.constData()) != 1) {
            lastError = QString("设置属性失败：%1 = %2").arg(path, value);
            return false;
        }
        return true;
    }
};

CanfdController::CanfdController(QObject *parent)
    : QObject(parent), pimpl(new Private) {}

CanfdController::~CanfdController()
{
    close();
}

bool CanfdController::open(const CanfdConfig &config)
{
    close();                            // 先复位，避免重复打开
    pimpl->config = config;
    pimpl->channelCount = config.channels < 1 ? 1 : (config.channels > 2 ? 2 : config.channels);

    // 1. 打开设备
    pimpl->dev = ZCAN_OpenDevice(config.deviceType, config.deviceIndex, 0);
    if (pimpl->dev == INVALID_DEVICE_HANDLE) {
        emit opened(false, QString("ZCAN_OpenDevice 失败"));
        return false;
    }
    pimpl->prop = GetIProperty(pimpl->dev);
    if (!pimpl->prop) {
        close();
        emit opened(false, QString("GetIProperty 失败"));
        return false;
    }

    // 2. 属性配置
    if (!pimpl->applyPropertyConfig()) {
        close();
        emit opened(false, pimpl->lastError);
        return false;
    }

    // 3. 初始化通道（CAN-FD 模式，硬件不过滤）
    ZCAN_CHANNEL_INIT_CONFIG cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.can_type = TYPE_CANFD;
    cfg.canfd.acc_code = 0;
    cfg.canfd.acc_mask = 0xFFFFFFFFU;
    cfg.canfd.brp = 0;
    cfg.canfd.filter = 0;
    cfg.canfd.mode = 0;

    for (int i = 0; i < pimpl->channelCount; i++) {
        pimpl->ch[i] = ZCAN_InitCAN(pimpl->dev, i, &cfg);
        if (pimpl->ch[i] == INVALID_CHANNEL_HANDLE) {
            close();
            emit opened(false, QString("ZCAN_InitCAN 失败（通道 %1）").arg(i));
            return false;
        }
    }

    // 4. 启动通道
    for (int i = 0; i < pimpl->channelCount; i++) {
        if (ZCAN_StartCAN(pimpl->ch[i]) != STATUS_OK) {
            close();
            emit opened(false, QString("ZCAN_StartCAN 失败（通道 %1）").arg(i));
            return false;
        }
    }

    pimpl->open = true;
    pimpl->epochCalibrated = false;   // 设备重新打开后重新校准时间偏移
    pimpl->rxTotal = 0;               // 计数清零
    pimpl->txTotal = 0;
    emit opened(true, QString("CAN-FD 设备打开成功（%1 通道）").arg(pimpl->channelCount));
    return true;
}

void CanfdController::close()
{
    if (pimpl->prop) {
        ReleaseIProperty(pimpl->prop);
        pimpl->prop = nullptr;
    }
    if (pimpl->dev != INVALID_DEVICE_HANDLE) {
        ZCAN_CloseDevice(pimpl->dev);
        pimpl->dev = INVALID_DEVICE_HANDLE;
    }
    for (auto &h : pimpl->ch)
        h = INVALID_CHANNEL_HANDLE;

    const bool wasOpen = pimpl->open;
    pimpl->open = false;
    if (wasOpen)
        emit closed();
}

bool CanfdController::isOpen() const
{
    return pimpl->open;
}

int CanfdController::channelCount() const
{
    return pimpl->channelCount;
}

QString CanfdController::deviceInfo() const
{
    if (!pimpl->open)
        return QString();
    ZCAN_DEVICE_INFO info;
    std::memset(&info, 0, sizeof(info));
    if (ZCAN_GetDeviceInf(pimpl->dev, &info) != STATUS_OK)
        return QString();
    return QString("%1 SN:%2")
        .arg(QString::fromLocal8Bit(reinterpret_cast<const char *>(info.str_hw_Type)))
        .arg(QString::fromLocal8Bit(reinterpret_cast<const char *>(info.str_Serial_Num)));
}

bool CanfdController::transmit(uint8_t channel, const CanfdFrame &frame)
{
    if (!pimpl->open) {
        emit errorOccurred(QString("设备未打开，发送失败"));
        return false;
    }
    if (channel >= pimpl->channelCount || pimpl->ch[channel] == INVALID_CHANNEL_HANDLE) {
        emit errorOccurred(QString("无效通道号：%1").arg(channel));
        return false;
    }

    CHANNEL_HANDLE h = pimpl->ch[channel];
    uint8_t frame_len = frame.len;
    if (!frame.isFd) {
        if (frame_len > 8) {
            emit errorOccurred(QString("经典 CAN 帧最多 8 字节"));
            return false;
        }
        ZCAN_Transmit_Data d;
        std::memset(&d, 0, sizeof(d));
        d.frame.can_id = frame.id;
        d.frame.can_dlc = frame_len;
        if (frame_len > 0 && !frame.data.isEmpty())
            std::memcpy(d.frame.data, frame.data.constData(), frame_len);
        d.transmit_type = frame.transmitType;   // 发送方式（0=正常，2=自发自收）
        if (ZCAN_Transmit(h, &d, 1) != 1) {
            emit errorOccurred(QString("ZCAN_Transmit 失败"));
            return false;
        }
        pimpl->txTotal++;
        return true;
    }

    if (frame_len > 64) {
        emit errorOccurred(QString("CAN-FD 帧最多 64 字节"));
        return false;
    }
    ZCAN_TransmitFD_Data d;
    std::memset(&d, 0, sizeof(d));
    d.frame.can_id = frame.id;
    d.frame.len    = frame.len;
    d.frame.flags  = frame.flags & 0x0FU;
    if (frame.len > 0 && !frame.data.isEmpty())
        std::memcpy(d.frame.data, frame.data.constData(), frame.len);
    d.transmit_type = frame.transmitType;   // 发送方式（0=正常，2=自发自收）
    if (ZCAN_TransmitFD(h, &d, 1) != 1) {
        emit errorOccurred(QString("ZCAN_TransmitFD 失败"));
        return false;
    }
    pimpl->txTotal++;

    return true;
}

int CanfdController::receive(int channel, QList<CanfdFrame> &frames)
{
    frames.clear();
    if (!pimpl->open)
        return 0;
    if (channel >= pimpl->channelCount || pimpl->ch[channel] == INVALID_CHANNEL_HANDLE)
        return 0;

    CHANNEL_HANDLE h = pimpl->ch[channel];
    static ZCAN_Receive_Data   canBuf[kMaxBatch];
    static ZCAN_ReceiveFD_Data fdBuf[kMaxBatch];

    // 经典 CAN 帧
    uint32_t n = ZCAN_GetReceiveNum(h, TYPE_CAN);
    if (n > 0) {
        uint32_t want = n > kMaxBatch ? kMaxBatch : n;
        uint32_t r = ZCAN_Receive(h, canBuf, want, 0);
        for (uint32_t i = 0; i < r; i++) {
            CanfdFrame frame;
            frame.id  = canBuf[i].frame.can_id;
            frame.len = canBuf[i].frame.can_dlc;
            frame.channel = channel;
            frame.timestampUs = canBuf[i].timestamp;
            frame.data = QByteArray(reinterpret_cast<const char *>(canBuf[i].frame.data), frame.len);
            frames.append(frame);
        }
    }

    // CAN-FD 帧
    n = ZCAN_GetReceiveNum(h, TYPE_CANFD);
    if (n > 0) {
        uint32_t want = n > kMaxBatch ? kMaxBatch : n;
        uint32_t r = ZCAN_ReceiveFD(h, fdBuf, want, 0);
        for (uint32_t i = 0; i < r; i++) {
            CanfdFrame frame;
            frame.id    = fdBuf[i].frame.can_id;
            frame.len   = fdBuf[i].frame.len;
            frame.flags = fdBuf[i].frame.flags;
            frame.channel = channel;
            frame.isFd = true;
            frame.timestampUs = fdBuf[i].timestamp;
            frame.data = QByteArray(reinterpret_cast<const char *>(fdBuf[i].frame.data), frame.len);
            frames.append(frame);
        }
    }
    // 设备 us 时间戳 -> 主机 epoch ms（用本批最新帧校准偏移，之后逐帧换算）
    if (!pimpl->epochCalibrated && !frames.isEmpty() && frames.last().timestampUs > 0) {
        pimpl->epochOffsetMs = QDateTime::currentMSecsSinceEpoch() - (qint64)(frames.last().timestampUs / 1000);
        pimpl->epochCalibrated = true;      // 标记校准完毕    
    }
    if (pimpl->epochCalibrated) {
        for (CanfdFrame &f : frames) {      // 整数除法，保证精度，时间戳的数值很大！
            if (f.timestampUs > 0)
                f.timestampEpochMs = (qint64)(f.timestampUs / 1000) + pimpl->epochOffsetMs;
        }
    }
    pimpl->rxTotal += (uint32_t)frames.size();

    return frames.size();
}

bool CanfdController::isActive() const
{
    if (!pimpl->open || !pimpl->dev)    return false;
    uint32_t st = ZCAN_IsDeviceOnLine(pimpl->dev);
    // 不同固件版本返回 STATUS_ONLINE(2) 或 STATUS_OK(1)，两者都视为在线
    return (st == STATUS_ONLINE) || (st == STATUS_OK);
}

uint32_t CanfdController::rxFrameCount() const
{
    return pimpl->rxTotal;
}

uint32_t CanfdController::txFrameCount() const
{
    return pimpl->txTotal;
}

uint32_t CanfdController::channelErrorCode(int channel) const
{
    if (!pimpl->open || channel >= pimpl->channelCount || pimpl->ch[channel] == INVALID_CHANNEL_HANDLE)
        return 0;
    ZCAN_CHANNEL_ERR_INFO err;
    std::memset(&err, 0, sizeof(err));
    if (ZCAN_ReadChannelErrInfo(pimpl->ch[channel], &err) != STATUS_OK)
        return 0;
    return err.error_code;
}