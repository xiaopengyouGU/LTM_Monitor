#ifndef WINDOW_DATA_PROCESSOR_H__
#define WINDOW_DATA_PROCESSOR_H__

#include <QObject>
#include <QRegularExpression>
#include <QVector>

#include "canfd.h"
#include "data_map.h"
#include "canfd_frame_model.h"

class QTimer;
class ChartManager;
class SensorData;
struct pid_data_t;             //前向声明

// 这个对象负责处理数据采样线程发送的高频数据，运行在独立线程中，
// 减轻UI主线程的处理工作量，避免出现丢指令或界面卡顿
// 定位：中转站。串口 LTM 直通分发；CAN-FD 走 缓冲 -> 排水 -> DataMap 解码 -> 图表

class WindowDataProcessor : public QObject
{
    Q_OBJECT
public:
    explicit WindowDataProcessor(QObject *parent = nullptr);
    void setManager(ChartManager *manager);                   //设置图表管理器
    void setPidNum(QList<pid_data_t> *pidNum);                //设置PID数据数组

    // CAN-FD 相关接口
    bool loadCanfdProtocol(const QString &filePath, QString *error = nullptr); // 加载 DBC / JSON 协议
    void clearCanfd();                                        // 清空缓冲与保持值

public slots:
    void do_serialDataUpdated(uint8_t data_type, const QByteArray& data); //串口接收数据更新
    void do_canfdDataUpdated(const QList<CanfdFrame> &frames);  // CAN-FD 帧入站（先缓冲，不直接处理）
    void do_canfdFramesSent(const QList<CanfdFrame> &frames);   // CAN-FD 已发送帧入站（Tx 回显）

signals:
    void textOrCMDReceived(uint8_t type, const QString& str, const QByteArray& data);   //读取到文本或命令
    void pidActualChanged(int ch, const QString& actual);       //实际值变动
    void pidActualChanged(const QStringList& actNum);           //实际值变动
    void canfdRowsReceived(const QList<CanfdFrameRow> &rows);   // CAN-FD 解析后的表格行转发
    void canfdRowsSent(const QList<CanfdFrameRow> &rows);       // CAN-FD 已发送帧的表格行（Tx 回显）
    void canfdDropped(int dropped, uint64_t total);             // 缓冲已满丢弃帧（累计）

private slots:
    void do_canfdDrain();                                     // 排水：解码并写入图表

private:
    QString data2Str(float value);                      //将数据转换为String,动态显示小数位
    void drainToChart(const QList<DataMapDecodedSignal> &sigs, qint64 arrivalMs);   // 解码结果投图（采样保持）

    static constexpr int CURVES_SIZE = 5;               //支持的曲线数量
    static constexpr int MIN_PERIOD = 39;               //至少间隔 39ms 发送一次数据到 UI主线程
    static constexpr int CANFD_BUF_MAX = 512;           // CAN-FD 入站缓冲上限（有界，防内存膨胀）
    static constexpr int CANFD_DRAIN_THRESHOLD = 256;   // 缓冲达到该数量立即排水（应对突发）
    static constexpr int CANFD_DRAIN_MS = 20;           // 排水定时器周期（应对涓流）
    QRegularExpression trailingZeros;                   //正则表达式
    QRegularExpression trailingDot;

    // CAN-FD 缓冲与解码（槽与定时器同在 process_thread，无需加锁）
    QList<CanfdFrame>   m_canBuf;
    QTimer             *m_canDrainTimer;
    DataMap             m_dataMap;
    uint64_t            m_canfdDropCount = 0;   // CAN-FD 缓冲丢弃帧累计
    float               m_holdValue[CURVES_SIZE][2] = {};   // 通道 x 目标/实际，采样保持
    bool                m_holdValid[CURVES_SIZE][2] = {};

private:
    ChartManager    *m_manager;
    QList<pid_data_t> *m_pidNum;

};

#endif
