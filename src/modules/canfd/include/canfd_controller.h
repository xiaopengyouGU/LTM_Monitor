#ifndef CANFD_CONTROLLER_H
#define CANFD_CONTROLLER_H

#include <QList>
#include <QObject>
#include "canfd_def.h"

// CAN-FD 控制器：厂商 SDK 的唯一封装层（领域级接口）。
// 后续移植其他厂商驱动时，只需修改本类，Worker / Manager 无需任何改动。
class CANFD_EXPORT CanfdController : public QObject
{
    Q_OBJECT
public:
    explicit CanfdController(QObject *parent = nullptr);
    ~CanfdController();

    bool open(const CanfdConfig &config);               // 打开设备、属性配置、初始化并启动通道
    void close();                                       // 关闭设备
    bool isOpen() const;
    int  channelCount() const;
    bool isActive() const;                              // 设备是否在线
    uint32_t rxFrameCount() const;                      // 自打开累计接收帧数
    uint32_t txFrameCount() const;                      // 自打开累计发送帧数
    uint32_t channelErrorCode(int channel) const;       // 读取通道错误码（读后清零），0 = 无错误
    QString  deviceInfo() const;                        // 设备信息：硬件型号 + 序列号

    bool transmit(uint8_t channel, const CanfdFrame &frame);   // 发送一帧，自动区分 CAN/CAN-FD
    int  receive(int channel, QList<CanfdFrame> &frames);      // 读取该通道的所有帧

signals:
    void opened(bool success, const QString &msg);
    void closed();
    void errorOccurred(const QString &msg);

private:
    class Private;
    Private *pimpl;
};

#endif // CANFD_CONTROLLER_H
