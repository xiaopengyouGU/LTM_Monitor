#include "data_hub.h"
#include <functional>
#include "chart.h"
#include "serial.h"
#include "canfd.h"
#include "data_map.h"
#include "chart_map.h"
#include "ltm_protocol.h"
#include "modbus_master.h"

#include <cstring>
#include <QRegularExpression>
#include <QTimer>
#include <chrono>

using namespace std::chrono;

// 单调时钟（double ms）：与 chart 模块 DataStorage 内部时间戳同源。
// 只测间隔/节流，不受系统时间修改（NTP/手动改时间）影响。
static double steadyMs()
{
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// ============================================================
// 私有实现（Pimpl）：缓冲/映射表/排水定时器/节流
// ============================================================
class DataHub::Private 
{
public:
    explicit Private(DataHub *hub) : hub(hub) {}

    static constexpr int CURVES_SIZE = MAX_CHANNEL_SIZE;   // 支持的曲线数量
    static constexpr int MIN_PERIOD = 47;                  // 至少间隔 47ms 发送一次数据到 UI主线程
    static constexpr int CANFD_BUF_MAX = 256;              // CAN-FD 入站缓冲上限（有界，防内存膨胀）
    static constexpr int CANFD_DRAIN_THRESHOLD = 128;      // 缓冲达到该数量立即排水（应对突发）
    static constexpr int CANFD_DRAIN_MS = 32;              // 排水定时器周期（应对涓流）
    static constexpr qint64 CANFD_STALE_US = 1000000;       // 积压帧阈值：与最新帧时间戳差 >1s 视为历史积压，图表路径丢弃

    void setManager(ChartManager *manager)                     { if (manager) m_manager = manager; }
    void setActualSink(std::function<void(int, float)> sink)   { m_actualSink = sink; }
    bool loadChartMap(const QString &filePath, QString *error) { return m_chartMap.loadJson(filePath, error); }
    ChartMap &chartMap() { return m_chartMap; }
    bool loadCanfdProtocol(const QString &filePath, QString *error);
    void clearCanfd();
    void setUpgradeMode(bool on) { m_canUpgradeMode = on; }     // 仅 data_thread 访问（槽运行在工作线程）
    void setSendChannels(SerialManager *serial, CanfdManager *canfd) { m_serial = serial; m_canfd = canfd; }
    void setSerialOnline(bool on) { m_serialOnline = on; }
    void setSerialProtocol(int type) { m_serialProtocol = type; }
    void setModbusMaster(ModbusMaster *master) { m_modbusMaster = master; }
    void sendLtm(uint8_t type, const QByteArray &data);
    void startPeriodSendLtm(uint8_t type, const QByteArray &data, int intervalMs);
    void stopPeriodSendLtm();
    void do_periodLtmTimeout();

    void do_serialDataUpdated(const QByteArray &bytes);
    void do_canfdDataUpdated(const QList<CanfdFrame> &frames);
    void do_canfdFramesSent(const QList<CanfdFrame> &frames);
    void do_canfdDrain();                                       // 排水：解码并写入图表
    void handleLtmFrame(uint8_t type, const QByteArray &data, qint64 canfdTsUs = -1);  // LTM 帧统一处理；CAN-FD 传设备 µs 时间戳，串口传 -1
    void drainToChart(const QList<DataMapDecodedSignal> &sigs, qint64 tsUs, quint64 sourceKey);

    DataHub *hub = nullptr;
    ChartManager *m_manager = nullptr;
    std::function<void(int, float)> m_actualSink;
    QList<double> m_channelValues;         // 各通道最新值（中转站 -> 图表控制器实际值列）

    // CAN-FD 缓冲与解码（槽与定时器同在 data_thread，无需加锁）
    QList<CanfdFrame>   m_canBuf;
    QTimer             *m_canDrainTimer = nullptr;
    DataMap             m_dataMap;         // 数据映射表
    LtmProtocol         ltmCanfd;          // LTM-over-CANFD：0x100 载荷重组解析（components 公共组件）
    LtmProtocol         ltmSerial;         // 串口 LTM 帧解析（上层组合点）
    int                 m_serialProtocol = Prot_LTM;   // 串口协议模式（LTM/普通/Modbus）
    ModbusMaster       *m_modbusMaster = nullptr;      // Modbus 主站事务器（可空）
    QTimer             *m_periodLtmTimer = nullptr;   // LTM 周期发送定时器（双通道路由）
    uint8_t             m_periodLtmType = 0;
    QByteArray          m_periodLtmData;
    ChartMap            m_chartMap;        // 统一图表映射表（数据源信号 → 10 槽）
    uint64_t            m_canfdDropCount = 0;      // CAN-FD 缓冲丢弃帧累计
    bool                m_canUpgradeMode = false;  // UDS 升级模式（仅 data_thread 访问）
    SerialManager      *m_serial = nullptr;        // 发送路由：串口（主窗口注入）
    CanfdManager       *m_canfd = nullptr;         // 发送路由：CAN-FD（主窗口注入）
    bool                m_serialOnline = false;    // 串口在线状态（主窗口同步）
    qint64              m_baseUs = -1;     // CANFD 相对时间基准（设备 µs）：首帧到达时刻，图表不再用 Epoch

    // 节流/时间状态
    double              m_serialMyTime = 0.0;      // 串口帧内部时间（多通道同步）
    double              m_serialFrameLastMs = -1.0;// 上一帧真实到达时刻（ms），时间戳钳制基准
    double              m_serialTsLast = 0.0;      // 串口实际值节流时间戳
    double              m_canTsLast    = 0.0;      // CAN-FD 实际值节流时间戳
};

// ============================================================
// 公共接口：全部委托给私有实现
// ============================================================
DataHub::DataHub(QObject *parent) : QObject(parent), pimpl(new Private(this))
{
    pimpl->m_channelValues.fill(qQNaN(), Private::CURVES_SIZE);   // 无数据通道显示 "-"
    pimpl->m_serialTsLast = steadyMs();
    pimpl->m_canTsLast    = pimpl->m_serialTsLast;

    // CAN-FD 排水定时器（与槽函数同在 data_thread 中运行）
    QTimer * canDrainTimer = new QTimer(this); 
    pimpl->m_canDrainTimer = canDrainTimer;
    canDrainTimer->setInterval(Private::CANFD_DRAIN_MS);
    canDrainTimer->setSingleShot(false);
    canDrainTimer->stop();
    connect(canDrainTimer, &QTimer::timeout, this,
            [this]() { pimpl->do_canfdDrain(); });
    // LTM 周期发送定时器（同在 data_thread，触发时走统一双通道路由）
    QTimer * periodLtmTimer = new QTimer(this);
    pimpl->m_periodLtmTimer = periodLtmTimer;
    periodLtmTimer->setSingleShot(false);
    periodLtmTimer->stop();
    connect(periodLtmTimer, &QTimer::timeout, this,
            [this]() { pimpl->do_periodLtmTimeout(); });
}

DataHub::~DataHub()                                 { delete pimpl; }
void DataHub::setManager(ChartManager *manager)     { pimpl->setManager(manager); }
void DataHub::setActualSink(std::function<void(int, float)> sink)  { pimpl->setActualSink(sink); }
bool DataHub::loadChartMap(const QString &filePath, QString *error){ return pimpl->loadChartMap(filePath, error); }
ChartMap &DataHub::chartMap()                       { return pimpl->chartMap(); }
bool DataHub::loadCanfdProtocol(const QString &filePath, QString *error) { return pimpl->loadCanfdProtocol(filePath, error); }
void DataHub::clearCanfd()                          { pimpl->clearCanfd(); }
void DataHub::setUpgradeMode(bool on)               { pimpl->setUpgradeMode(on); }

void DataHub::setSendChannels(SerialManager *serial, CanfdManager *canfd) { pimpl->setSendChannels(serial, canfd); }
void DataHub::setSerialOnline(bool on)              { pimpl->setSerialOnline(on); }
void DataHub::setSerialProtocol(int type)           { pimpl->setSerialProtocol(type); }
void DataHub::setModbusMaster(ModbusMaster *master) { pimpl->setModbusMaster(master); }
void DataHub::startPeriodSendLtm(uint8_t type, const QByteArray &data, int intervalMs)
{ pimpl->startPeriodSendLtm(type, data, intervalMs); }
void DataHub::stopPeriodSendLtm() { pimpl->stopPeriodSendLtm(); }
void DataHub::sendLtm(uint8_t type, const QByteArray &data) { pimpl->sendLtm(type, data); }

void DataHub::do_serialDataUpdated(const QByteArray &bytes)       { pimpl->do_serialDataUpdated(bytes); }
void DataHub::do_canfdDataUpdated(const QList<CanfdFrame> &frames){ pimpl->do_canfdDataUpdated(frames); }
void DataHub::do_canfdFramesSent(const QList<CanfdFrame> &frames) { pimpl->do_canfdFramesSent(frames); }

// ============================================================
// 私有实现细节
// ============================================================
void DataHub::Private::handleLtmFrame(uint8_t type, const QByteArray &data, qint64 canfdTsUs)
{
    switch (type)
    {
        case Data_Channel_ALL:                      // 接收单帧多通道数据
        {
            if (!m_manager)         return;         // 未设置图表管理器，直接返回
            float values[CURVES_SIZE];
            memcpy(values, data.constData(), qMin(data.size(), (qsizetype)sizeof(values)));   // 防止帧长超过数组越界
            const double ts_now = steadyMs();       // 每帧壁钟时刻（节流基准）
            int cnt = (int)data.size() >> 2;        // 除以4字节（float）
            cnt = qMin(cnt, CURVES_SIZE);

            // 记录全部通道最新值（实际值列覆盖全部接收通道）
            for (int i = 0; i < cnt; ++i)
                m_channelValues[i] = values[i];

            // 帧时间戳：
            //   CAN-FD（canfdTsUs >= 0）：用设备 µs 高精度时间戳（相对基准 m_baseUs）
            //   串口：2.5ms 钳制推进（readAll 多帧伪间隔保证单调）
            double frameTime;
            if (canfdTsUs >= 0) {
                if (m_baseUs < 0 && canfdTsUs > 0)
                    m_baseUs = canfdTsUs;                       // 首帧设备时间戳即零点
                frameTime = (canfdTsUs > 0 && m_baseUs >= 0) ? double(canfdTsUs - m_baseUs) / 1e6 : -1.0;
            } else {
                frameTime = m_serialMyTime;                     // 当前帧时间（推进前）
                if (m_serialFrameLastMs >= 0) {
                    double dt = ts_now - m_serialFrameLastMs;
                    if (dt < 2.5) dt = 2.5;                     // 钳制下限，时间戳严格单调
                    m_serialMyTime += dt / 1000.0;
                }
                m_serialFrameLastMs = ts_now;
            }

            // 只投递帧内实际通道（默认 CH0-CH4）：未接收的通道不写入存储，
            // 否则导出/存储会带上 32 列全量通道（且 values[i] 为未初始化垃圾值）
            QList<ChannelData> frameData;
            frameData.reserve(cnt);
            for (int i = 0; i < cnt; ++i) {
                ChannelData cd;
                cd.channel = i;
                cd.times.append(frameTime);                     // 批量写入统一取该帧时间戳（多通道同步）
                cd.values.append(values[i]);
                frameData.append(cd);
            }
            m_manager->addData(frameData);                      // 一帧多通道共享同一时间戳
            // 节流：记录上次 emit 时刻，间隔 >= MIN_PERIOD 再发（壁钟基准）
            if ((ts_now - m_serialTsLast) >= MIN_PERIOD) {
                m_serialTsLast = ts_now;
                // PID 实际值强写（只写数据）+ 一次刷新通知 + 图表实际值列
                for (int i = 0; i < PID_CHANNEL_SIZE; ++i) {
                    if (i >= cnt)   break;
                    if (m_actualSink) m_actualSink(i, m_channelValues[i]);
                }
                emit hub->pidActualChanged();                       // 通知 PidWidget 刷新界面
                emit hub->channelActualChanged(m_channelValues);
            }
            break;
        }
        case Data_CMD_Text:
            emit hub->textOrCMDReceived(type, QString(), data);
            break;
        case Data_CMD_Start:                            // 下位机响应启动
            emit hub->textOrCMDReceived(type, "下位机启动成功", data);
            break;
        case Data_CMD_Stop:                             // 下位机响应停止
            emit hub->textOrCMDReceived(type, "下位机停机成功", data);
            break;
        default: break;
    }
}

void DataHub::Private::sendLtm(uint8_t type, const QByteArray &data)
{
    /* 路由：串口在线优先（既有行为），否则 CAN-FD 0x100（LTM-over-CANFD） */
    const QByteArray frame = ltmSerial.package(type, data);  // LTM 帧组装统一在上层（DataHub）
    if (m_serialOnline && m_serial)
        m_serial->send(frame);                              // 串口：字节直发
    else if (m_canfd && m_canfd->isActive()) {
        // CAN-FD：直接组装 0x100 帧（64B 分片，不经任何 canfd 协议接口）
        int off = 0;
        while (off < frame.size()) {
            const int chunk = qMin(64, frame.size() - off);
            CanfdFrame f;
            f.id    = LTM_CANFD_DATA_ID;
            f.isFd  = true;
            f.flags = 0x01;                                 // BRS 加速
            f.len   = (uint8_t)chunk;
            f.data  = frame.mid(off, chunk);
            m_canfd->send(f);
            off += chunk;
        }
    }
}

void DataHub::Private::startPeriodSendLtm(uint8_t type, const QByteArray &data, int intervalMs)
{
    m_periodLtmTimer->stop();
    m_periodLtmType = type;
    m_periodLtmData = data;
    if (intervalMs <= 0)
        return;                                     // 非法间隔，仅停止
    m_periodLtmTimer->start(intervalMs);
}

void DataHub::Private::stopPeriodSendLtm()
{
    m_periodLtmTimer->stop();
}

void DataHub::Private::do_periodLtmTimeout()
{
    sendLtm(m_periodLtmType, m_periodLtmData);      // 走统一路由：串口优先，否则 CAN-FD 0x100
}

void DataHub::Private::do_serialDataUpdated(const QByteArray &bytes)
{
    /* 串口字节按协议模式分发（上层组合点），每帧独立处理 */
    switch (m_serialProtocol) {
    case Prot_Common:
        handleLtmFrame(Data_CMD_Text, bytes);       // 普通串口：原始字节当文本
        break;
    case Prot_Modbus:
        if (m_modbusMaster)
            m_modbusMaster->receive(bytes);         // Modbus 响应交给主站事务器
        break;
    default:                                        // LTM 协议
        ltmSerial.receive(bytes);
        uint8_t type;
        QByteArray data;
        while (ltmSerial.process(type, data))      // 数据解析
            handleLtmFrame(type, data);            // 与 CAN-FD 封装共用同一处理路径
        break;
    }
}

/********************************** CAN-FD 中转 ************************************/
bool DataHub::Private::loadCanfdProtocol(const QString &filePath, QString *error)
{
    bool ok = false;
    if (filePath.endsWith(".dbc", Qt::CaseInsensitive))
        ok = m_dataMap.loadDbc(filePath, error);
    else
        ok = m_dataMap.loadJson(filePath, error);
    if (!ok) return false;

    // 协议内嵌的图表映射并入统一映射表（重新加载以新文件为准）
    m_chartMap.clear();
    const QList<DataMapMessage> msgs = m_dataMap.messages();
    for (const DataMapMessage &m : msgs) {
        const quint64 key = ((quint64)(m.isExt ? 1 : 0) << 32) | m.id;
        for (int i = 0; i < m.signalList.size(); i++) {
            const DataMapSignal &sig = m.signalList.at(i);
            if (sig.chartChannel < 1 || sig.chartChannel > CURVES_SIZE)
                continue;                       // 0 = 不映射（仅表格/日志）
            ChartMapEntry entry;
            entry.sourceType  = ChartMap_CANFD;
            entry.sourceKey   = key;
            entry.signalIndex = (quint16)i;
            entry.signalName  = sig.name;
            entry.channel     = (quint8)sig.chartChannel;
            QString err;
            m_chartMap.addEntry(entry, &err);   // 槽位冲突按配置处理，单条失败忽略
        }
    }
    return true;
}

void DataHub::Private::clearCanfd()
{
    m_canBuf.clear();
    m_canDrainTimer->stop();
    m_baseUs = -1;                              // 设备 µs 计数器可能归零，重新建立相对基准
}

void DataHub::Private::do_canfdDataUpdated(const QList<CanfdFrame> &frames)
{
    /* 升级模式：仅转发原始帧给 UDS，旁路表格/图表解码；
     * 非升级时 UDS 不活跃，不发，省一次高频跨线程投递 */
    if (m_canUpgradeMode) {
        emit hub->canfdRawReceived(frames);
        return;
    }

    // 中转站职责：把原始帧解析为表格行（格式化在源头做一次），转发给表格
    QList<CanfdFrameRow> rows;
    rows.reserve(frames.size());
    for (const CanfdFrame &f : frames)
        rows.append(CanfdFrameRow::fromFrame(f, false));
    emit hub->canfdRowsReceived(rows);

    int dropped = 0;
    for (const CanfdFrame &f : frames) {
        if (m_canBuf.size() >= CANFD_BUF_MAX) {
            dropped++;                          // 缓冲已满，丢弃剩余新帧
            continue;
        }
        m_canBuf.append(f);
    }
    if (dropped > 0) {
        m_canfdDropCount += dropped;
        emit hub->canfdDropped(dropped, m_canfdDropCount);
    }
    if (m_canBuf.size() >= CANFD_DRAIN_THRESHOLD)
        do_canfdDrain();                        // 突发：达到阈值立即排水
    else if (!m_canDrainTimer->isActive())
        m_canDrainTimer->start();               // 涓流：定时器兜底
}

void DataHub::Private::do_canfdFramesSent(const QList<CanfdFrame> &frames)
{
    // 与接收同源：帧 -> 表格行（格式化在源头做一次），发给表格
    QList<CanfdFrameRow> rows;
    rows.reserve(frames.size());
    for (const CanfdFrame &f : frames)
        rows.append(CanfdFrameRow::fromFrame(f, true));
    emit hub->canfdRowsSent(rows);
}

void DataHub::Private::do_canfdDrain()
{
    if (m_canBuf.isEmpty()) {
        m_canDrainTimer->stop();
        return;
    }

    QList<CanfdFrame> frames;
    frames.swap(m_canBuf);                      // 整批取出，避免逐帧拷贝
    if (m_canBuf.isEmpty())
        m_canDrainTimer->stop();

    // 积压帧过滤：USB-CANFD 恢复读取时，缓冲里混着历史老帧与当前新帧，
    // 时间戳差 = 积压时长。先找批次最新时间戳，远早于它的帧丢弃，避免图表时间轴突跳
    qint64 maxTs = 0;
    for (const CanfdFrame &f : frames)
        if ((qint64)f.timestampUs > maxTs) maxTs = (qint64)f.timestampUs;

    for (const CanfdFrame &frame : frames) {
        if (maxTs > 0 && frame.timestampUs > 0 && (maxTs - (qint64)frame.timestampUs) > CANFD_STALE_US)
            continue;                           // 历史积压帧：丢弃（图表路径）
        // LTM-over-CANFD：0x101 上行帧载荷 = LTM 帧字节流（电机上报）
        // （可跨帧分片，LtmProtocol 按帧头/长度自动重组）
        if (frame.id == LTM_CANFD_SEND_ID) {
            ltmCanfd.receive(frame.data);
            uint8_t ltmType;
            QByteArray ltmData;
            while (ltmCanfd.process(ltmType, ltmData))
                handleLtmFrame(ltmType, ltmData, (qint64)frame.timestampUs);   // CAN-FD 用设备 µs 时间戳
            continue;                               // 不进入 DataMap 解码
        }
        QList<DataMapDecodedSignal> sigs;
        QString err;
        const quint64 sourceKey = ((quint64)(frame.isEff() ? 1 : 0) << 32) | frame.id;   // 与 DataMap 消息键一致
        if (m_dataMap.decode(frame.id, frame.isEff(), frame.data, sigs, &err))
            drainToChart(sigs, (qint64)frame.timestampUs, sourceKey);   // 设备 µs 时间戳，不用 Epoch
        // 未定义的 ID 直接忽略，保持通道上一个有效值
    }
}

void DataHub::Private::drainToChart(const QList<DataMapDecodedSignal> &sigs,
                                                qint64 tsUs, quint64 sourceKey)
{
    if (!m_manager) return;

    // 应用层相对基准（µs）：首帧设备时间戳即零点，图表不再使用 Epoch
    if (m_baseUs < 0 && tsUs > 0)
        m_baseUs = tsUs;
    const double t = (tsUs > 0 && m_baseUs >= 0) ? double(tsUs - m_baseUs) / 1e6 : -1.0;   // 秒；无有效时间戳走自动

    // 统一映射表驱动：信号 → 单值通道（1..CURVES_SIZE 槽），整帧共享同一帧时间
    QList<ChannelData> frameData;
    frameData.reserve(sigs.size());
    for (const DataMapDecodedSignal &s : sigs) {
        const ChartMapEntry e = m_chartMap.findBySource(ChartMap_CANFD, sourceKey, s.signalIndex);
        if (e.channel < 1 || e.channel > CURVES_SIZE)
            continue;                           // 0 = 不映射（仅表格/日志）
        ChannelData cd;
        cd.channel = e.channel - 1;
        cd.times.append(t);
        cd.values.append(s.value);
        frameData.append(cd);
        m_channelValues[e.channel - 1] = s.value;           // 记录通道最新值
    }
    m_manager->addData(frameData);              // 一帧多信号：每个映射信号投到自己的单值通道

    // 与串口同款节流：中转站统一节奏发实际值给图表控制器
    const double ts_now = steadyMs();
    if ((ts_now - m_canTsLast) >= MIN_PERIOD) {
        m_canTsLast = ts_now;
        emit hub->channelActualChanged(m_channelValues);
    }
}
