#ifndef DATA_HUB_H__
#define DATA_HUB_H__

#include <QObject>
#include <cstdint>
#include <functional>
#include "canfd_frame_model.h"

#define MAX_CHANNEL_SIZE        32    // 上位最多支持32条通道（对应128字节，float)，LTM 协议上限
#define PID_CHANNEL_SIZE        5     // pid 对应通道数

class ChartManager;                   // 前向声明，提高编译速度
class ChartMap;
class SerialManager;
class CanfdManager;
class ModbusMaster;

// 这个对象负责处理数据采样线程发送的高频数据，运行在独立线程中，
// 减轻UI主线程的处理工作量，避免出现丢指令或界面卡顿
// 定位：数据中转站。串口 LTM 直通分发；CAN-FD 走 缓冲 -> 排水 -> DataMap 解码 -> 图表

class DataHub : public QObject
{
    Q_OBJECT
public:
    explicit DataHub(QObject *parent = nullptr);
    ~DataHub();
    void setManager(ChartManager *manager);                   // 设置图表管理器
    void setActualSink(std::function<void(int, float)> sink); // 实际值强写回调（PidWidget::setValue）
    bool loadChartMap(const QString &filePath, QString *error = nullptr);  // 加载图表映射表（JSON）
    ChartMap &chartMap();                                     // 映射表访问（编辑器/运行时共用）

    // CAN-FD 相关接口
    bool loadCanfdProtocol(const QString &filePath, QString *error = nullptr); // 加载 DBC / JSON 协议
    void clearCanfd();                                        // 清空缓冲与保持值
    void setUpgradeMode(bool on);                             // UDS 升级模式：旁路表格显示/图表解码，仅转发原始帧
    void setSendChannels(SerialManager *serial, CanfdManager *canfd);  // 发送通道注入（路由用）
    void setSerialOnline(bool on);                            // 串口在线状态（主窗口同步）
    void sendLtm(uint8_t type, const QByteArray &data);       // 统一发送数据（支持LTM和普通串口）：串口优先，否则 CAN-FD 0x100（LTM-over-CANFD）
    void setSerialProtocol(int type);                         // 串口协议模式（Prot_LTM / Prot_Common / Prot_Modbus）
    void setModbusMaster(ModbusMaster *master);               // Modbus 主站事务器挂接
    void startPeriodSendLtm(uint8_t type, const QByteArray &data, int intervalMs);  // 周期发送 LTM 帧
    void stopPeriodSendLtm();                                             // 停止周期发送 LTM 帧

public slots:
    void do_serialDataUpdated(const QByteArray &bytes);         // 串口接收原始字节（上层组合解析）
    void do_canfdDataUpdated(const QList<CanfdFrame> &frames);  // CAN-FD 帧入站（先缓冲，不直接处理）
    void do_canfdFramesSent(const QList<CanfdFrame> &frames);   // CAN-FD 已发送帧入站（Tx 回显）

signals:
    void textOrCMDReceived(uint8_t type, const QString& str, const QByteArray& data); // 读取到文本或命令
    void pidActualChanged();                                    // PID 实际值更新通知（数据已强写，一次信号刷新）
    void channelActualChanged(const QList<double>& values);     // 图表实际值列：32 通道最新值（节流发送）
    void canfdRawReceived(const QList<CanfdFrame> &frames);     // CAN-FD 原始帧转发（UDS 升级等需原始数据的模块）
    void canfdRowsReceived(const QList<CanfdFrameRow> &rows);   // CAN-FD 解析后的表格行转发
    void canfdRowsSent(const QList<CanfdFrameRow> &rows);       // CAN-FD 已发送帧的表格行（Tx 回显）
    void canfdDropped(int dropped, uint64_t total);             // 缓冲已满丢弃帧（累计）

private:
    class Private;
    Private *pimpl;
};

#endif
