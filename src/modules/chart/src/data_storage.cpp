#include "data_storage.h"
#include "chart_def.h"
#include "chart_manager.h"
#include <QtMath>
#include <QPair>
#include <QReadWriteLock>     // 读写锁，用于并发操作
#include <algorithm>
#include <chrono>             // 高精度计时模块，用于自动生成时间戳（s）
#include <atomic>             // 基准时间戳原子读写，跨线程安全
#include <array>
using namespace std::chrono;

// startIndex : 物理索引，pointCnt：数据点数，maxPoints：最大点数
// 因 startIndex < maxPoints, pointCnt <= maxPoints 
// 故 startIndex + pointCnt < 2 * maxPoints, 可用减法代替取模运算，提高性能

// 二分法逻辑索引查找
static int _binarySearchLogic(const QList<double>& num, int len, int startIndex, double target, bool isBig);
// ---------- 私有实现类 ----------
class DataStorage::Private : public QObject
{
    Q_OBJECT
public:
    Private(int maxPoints, int channelCount, DataStorage *parent);
    ~Private();

    // 公开给 DataStorage 调用的函数
    void addData(int ch, double time, double value);                   // 单次添加
    void addData(const QList<ChannelData>& dataNum);                   // 批量添加
    void resetData(int ch);                                            // 清空通道数据
    qint64 dataVersion() const;                                        // 数据版本号
    ChannelData getData(int channel, int lastN);                       // 获取最近的 N 个 数据
    // 获取降采样后数据，支持 M4 和 LTTB降采样。threshold <= 0 : 不降采样
    ChannelData getData(int ch, double startTime, double endTime, int threshold, bool isLTTB);
    // 降采样公共上下文：逻辑索引 -> 物理索引 取点/追加（LTTB 与 M4 共用）
    struct DownsampleCtx
    {
        const ChannelData& cd;
        ChannelData& sample;
        int          maxPoints;
        int          startIndex;
        QPair<double, double> point(int logic) const
        {
            int pos = startIndex + logic;
            int realIdx = (pos >= maxPoints) ? (pos - maxPoints) : pos;
            return QPair<double, double>(cd.times[realIdx], cd.values[realIdx]);
        }
        double yValue(int logic) const     // 用于 M4降采样，cache 友好
        {
            int pos = startIndex + logic;
            int realIdx = (pos >= maxPoints) ? (pos - maxPoints) : pos;
            return cd.values[realIdx];
        }
        void append(int logic)
        {
            QPair<double, double> p = point(logic);
            sample.times.append(p.first);
            sample.values.append(p.second);
        }
    };
private:
    // 内部辅助函数
    void _addData(int ch, double time, double value);                // 添加通道数据 
    int  _getPointCnt(int ch,  double startTime, double endTime,        
                 int& startIndex, int& leftLogic, int& rightLogic);  // 获取逻辑索引和数据点数
    void _lttbDownSample(DownsampleCtx& ctx, int startIndex, int leftLogic, int rightLogic, int threshold);
    void _m4DownSample(DownsampleCtx& ctx, int startIndex, int leftLogic, int rightLogic, int threshold);
private:
    struct InternalChannel {
        ChannelData data;                       // 对外可见的数据部分
        std::atomic<int> head{0};               // 原子读写，跨线程安全
        std::atomic<int> count{0};              // 环形缓冲区对应变量
    };
    int m_maxPoints;                            // 每个通道最大存储点数
    int m_channelCount;                         // 实际通道数
    std::array<InternalChannel, MAX_CHANNEL_NUMS> m_channels;// 数组（管理所有通道数据）
    QReadWriteLock m_rwLock;                    // 写入互斥锁
    std::atomic<double> m_baseTimestamp{-1.0};  // 基准时间戳（s），epoch时间（原子，跨线程安全）
    std::atomic<qint64> m_dataVersion{0};       // 数据版本号（写入/清空递增），刷新去重用
};

// ---------- Private 实现 ----------
DataStorage::Private::Private(int maxPoints, int channelCount, DataStorage *parent)
    : QObject(parent), m_maxPoints(maxPoints), m_channelCount(channelCount)
{
    // 通道数限制 1~60，默认 10
    if (m_channelCount < 1 || m_channelCount > MAX_CHANNEL_NUMS)
        m_channelCount = DEFUALT_CHANNEL_NUMS;
    // 通道点数设置，有最大点数限制
    if (maxPoints > MAX_CHANNEL_POINTS )         maxPoints = MAX_CHANNEL_POINTS;
    else if (maxPoints < MIN_CHANNEL_POINTS)     maxPoints = MIN_CHANNEL_POINTS;
    // 初始化每个通道的环形缓冲区
    for (int i = 0; i < m_channelCount; i++) {
        m_channels[i].data.times.reserve(maxPoints);
        m_channels[i].data.times.resize(maxPoints);     // 直接分配好内存，可下标访问
        m_channels[i].data.values.reserve(maxPoints);   // 先reserve，再resize，不会分配多余内存
        m_channels[i].data.values.resize(maxPoints);  
        m_channels[i].data.channel = i;
        m_channels[i].head    = 0;
        m_channels[i].count   = 0;
    }
    m_maxPoints = maxPoints;
}

DataStorage::Private::~Private()
{
    // 无需手动释放，父对象会自动销毁
}

// ============ 单次添加 ============
void DataStorage::Private::addData(int channel, double time, double value)
{
    // 1. 参数检查
    if (channel < 0 || channel >= m_channelCount) return;
    // 2. 首先添加数据，基准时间戳自动更新，epoch时间（s）；CAS 保证并发首写只生效一次
    double base = m_baseTimestamp.load(std::memory_order_relaxed);
    const double now = duration<double>(steady_clock::now().time_since_epoch()).count(); // 单位：秒
    if (base < 0.0) {
        double expect = -1.0;
        m_baseTimestamp.compare_exchange_strong(expect, now);
        base = m_baseTimestamp.load(std::memory_order_relaxed);
    }
    if (time < 0.0) {     // 采用内部时间戳。
        time = now - base;
    }
    // 3. 直接写入环形缓冲区
    QWriteLocker locker(&m_rwLock);
    _addData(channel, time, value);
    m_dataVersion.fetch_add(1, std::memory_order_relaxed);
}

// ============ 批量添加 ============
void DataStorage::Private::addData(const QList<ChannelData>& dataList) {
    // 1. 一次性加锁，批量写入
    QWriteLocker locker(&m_rwLock);
    // 2. 参数检查：逐项校验
    if (dataList.isEmpty())         return;         // 判空
    // 3. 首先添加数据，基准时间戳自动更新，epoch时间（s）；CAS 保证并发首写只生效一次
    const double now = duration<double>(steady_clock::now().time_since_epoch()).count(); // 单位：秒
    double base = m_baseTimestamp.load(std::memory_order_relaxed);
    if (base < 0.0) {
        double expect = -1.0;
        m_baseTimestamp.compare_exchange_strong(expect, now);
        base = m_baseTimestamp.load(std::memory_order_relaxed);   // 关键：CAS 后必须重载，否则首帧时间戳 = now+1
    }
    for (const auto& src : dataList) {
        ChannelData cd = src;                // 隐式共享拷贝：改写时间戳不污染调用方
        int channel = cd.channel;
        if (channel < 0 || channel >= m_channelCount)       continue;
        if (cd.times.size() != cd.values.size())            continue;  // 必须等长
        if (cd.values.isEmpty())                            continue;
        if (cd.times.size() == 1 && cd.times[0] < 0.0) {    // 单帧多通道数据，批量添加快捷操作
            cd.times[0] = now - base;                       // 用户无需输入时间戳，仅适用于单帧信号
        }
        for (int i = 0; i < cd.values.size(); i++) {
            _addData(channel, cd.times[i], cd.values[i]);
        }
    }
    m_dataVersion.fetch_add(1, std::memory_order_relaxed);
}

qint64 DataStorage::Private::dataVersion() const
{
    return m_dataVersion.load(std::memory_order_relaxed);
}

void DataStorage::Private::resetData(int channel)    // 清空通道数据
{
    if (channel < 0 || channel >= m_channelCount)       return;
    InternalChannel& cd = m_channels[channel];
    QWriteLocker locker(&m_rwLock);                  // 上锁
    cd.head  = 0;                                    // 清空环形缓冲区标志即可
    cd.count = 0;                                    // 
    m_dataVersion.fetch_add(1, std::memory_order_relaxed);
}

ChannelData  DataStorage::Private::getData(int channel, int lastN) // 获取最近的 N 个 数据
{
    ChannelData rawData;
    if (channel < 0 || channel >= m_channelCount || lastN <= 0)   return rawData;
    // 读取可以不上锁，首先数据读取极快（读新数据）。写入最多覆盖老数据，且缓冲区极大。可以容忍轻微不一致。
    const InternalChannel &cd = m_channels[channel];
    const ChannelData& data   = cd.data;
    const int count = cd.count;          // 只读一次
    const int head  = cd.head;           // 只读一次
    lastN = qMin(lastN, count);
    rawData.channel = channel;
    rawData.times.resize(lastN);            // 直接初始化内存，减少 append 开销 
    rawData.values.resize(lastN);

    // 最近 N 个 = 逻辑区间 [count-lastN, count)；未写满时逻辑 0 在物理 0，写满后 head 即逻辑 0
    int maxPoints  = m_maxPoints;
    int startIndex = (count < maxPoints) ? 0 : head;   // 逻辑 0 对应的物理索引
    const int firstLogic = count - lastN;              // 最近 N 个点的逻辑起点
    for (int i = 0; i < lastN; ++i) {
        int pos     = startIndex + firstLogic + i;
        int realIdx = (pos >= maxPoints) ? (pos - maxPoints) : pos;
        rawData.times[i]  = data.times[realIdx];
        rawData.values[i] = data.values[realIdx];
    }
    return rawData;
}

// 若输入 startTime < 0，endTime < 0 ==> 取所有数据。threshold <= 0, 不进行降采样。 
ChannelData DataStorage::Private::getData(int channel, double startTime, double endTime, int threshold, bool isLTTB)
{
    ChannelData sample;
    if (channel < 0 || channel >= m_channelCount)  return sample; 
    // 读取可以不上锁，首先数据读取极快（读新数据）。写入最多覆盖老数据，且缓冲区极大。可以容忍轻微不一致。
    // 获取时间范围内数据的逻辑索引范围  
    int startIndex, leftLogic, rightLogic;
    int pointCnt = _getPointCnt(channel, startTime, endTime, startIndex, leftLogic, rightLogic);
    if (!pointCnt)      return sample;      // 未找到数据，直接返回 

    const ChannelData &cd = m_channels[channel].data;
    int maxPoints  = m_maxPoints;
    sample.channel = channel;               // 记录通道值
    // 数据量 <= 阈值，或不启用降采样 ==> 直接取原始数据
    threshold = qMin(threshold, LTTB_THRESHOLD - 2);   // 数据量限制, 确保容量足够，留2，用于M4首尾数据添加
    if (threshold <= 0 || pointCnt <= threshold) {
        sample.times.resize(pointCnt);                 // 大小调整后，直接写入覆盖
        sample.values.resize(pointCnt);
        int index = 0;                
        for (int logic  = leftLogic; logic <= rightLogic; logic++) {
            int pos     = startIndex + logic; 
            int realIdx = (pos >= maxPoints) ? (pos - maxPoints) : pos;
            sample.times[index]  = cd.times[realIdx];
            sample.values[index] = cd.values[realIdx];
            index++;
        }
    } else {    
        sample.times.reserve(threshold + 2);    // 预分配内存，避免append过程中出现内存重新分配开销
        sample.values.reserve(threshold + 2);   // 留两个点用于 M4 降采样的首尾
        DownsampleCtx ctx{cd, sample, maxPoints, startIndex};
        if (isLTTB) {   // LTTB 降采样，精度高，但性能略差。
            _lttbDownSample(ctx, startIndex, leftLogic, rightLogic, threshold);
        } else {        // M4 降采样，精度适中，性能优异
            _m4DownSample(ctx, startIndex, leftLogic, rightLogic, threshold);
        }
    }
    return sample;
}

/************************************* 内部实现 **********************************/
// ============ 内部写入函数 ============
void DataStorage::Private::_addData(int channel, double time, double value) 
{   
    // 环形缓冲区已经提前分配好内存，并初始化为0了，可以直接下标访问
    InternalChannel& cd = m_channels[channel];
    ChannelData& data = cd.data;
    int index  = cd.head;
    int maxPoints      = m_maxPoints;
    data.times[index]  = time;
    data.values[index] = value;
    // 更新环形缓冲区写入头下标，此处可以用减法代替取模运算，提高性能
    index    = index + 1;
    cd.head  = (index >= maxPoints) ? (index - maxPoints) : index;
    if (cd.count < maxPoints) cd.count++;
}

// 利用二分查找快速返回目标访问的逻辑索引和点数（时间戳是 逻辑单调递增的，且 >= 0）
int DataStorage::Private::_getPointCnt(int ch, double startTime, double endTime,
                                   int& startIndex, int& leftLogic, int& rightLogic)
{
    const InternalChannel& cd = m_channels[ch];
    const ChannelData& data = cd.data;
    int len   = cd.count;
    int head  = cd.head;
    if (len == 0)                   return 0;       // 缓冲区中没有数据

    int maxPoints = m_maxPoints;
    startIndex = (len < maxPoints) ? 0 : head;      // 逻辑起点对应的物理索引
    int pos = startIndex + len - 1;                 // 用减法代替取模运算，性能更优
    int endIndex = (pos >= maxPoints) ? (pos - maxPoints) : pos;
    double earliest = data.times[startIndex];
    double latest   = data.times[endIndex];
    // 若输入 startTime < 0，endTime < 0 ==> 取所有数据
    if (startTime < 0.0 && endTime < 0.0) {
        startTime = earliest;
        endTime   = latest;
    }
    // 时间范围判别，并进行适当调整
    if (endTime < earliest || startTime > latest) return 0;
    if (startTime < earliest) startTime = earliest;
    if (endTime > latest)     endTime   = latest;

    leftLogic  = _binarySearchLogic(data.times,  len, startIndex, startTime, true);
    rightLogic = _binarySearchLogic(data.times,  len, startIndex, endTime, false);
    // 没找着正常的逻辑偏移值
    if (leftLogic == -1 || rightLogic == -1 || leftLogic > rightLogic) return 0;
    int pointCnt = rightLogic - leftLogic + 1;
    return pointCnt;                    // 返回最终数据点数
}

void DataStorage::Private::_lttbDownSample(DownsampleCtx &ctx, int startIndex, int leftLogic, int rightLogic, int threshold)
{
    const int pointCnt = rightLogic - leftLogic + 1;

    ctx.append(leftLogic);              // 第一个点
    if (threshold < 3)        return;   // 防 (threshold-2) 除零
    double bucketSize = (pointCnt - 2.0) / (threshold - 2.0);
    int a = leftLogic;
    for (int i = 0; i < threshold - 2; ++i) {
        int bucketStart = std::max(a + 1, leftLogic + (int)std::floor((i + 1) * bucketSize));
        int bucketEnd   = std::min(rightLogic, leftLogic + (int)std::floor((i + 2) * bucketSize));
        if (bucketStart >= bucketEnd) {
            if (bucketStart > rightLogic) break;
            ctx.append(bucketStart);
            a = bucketStart;
            continue;
        }
        // 计算下一个桶的平均点
        int avgRangeStart = bucketEnd;
        int avgRangeEnd   = (i == threshold - 3) ? rightLogic
                                                 : leftLogic + (int)std::floor((i + 3) * bucketSize);
        avgRangeEnd = std::min(avgRangeEnd, rightLogic);
        double avgX = 0, avgY = 0;
        int avgCount = avgRangeEnd - avgRangeStart + 1;
        if (avgCount > 0) {
            for (int k = avgRangeStart; k <= avgRangeEnd; ++k) {
                QPair<double, double> p = ctx.point(k);
                avgX += p.first;
                avgY += p.second;
            }
            avgX /= avgCount;
            avgY /= avgCount;
        } else {
            QPair<double, double> p = ctx.point(avgRangeStart);
            avgX = p.first;
            avgY = p.second;
        }

        double maxArea = -1;
        int maxAreaIndex = bucketStart;
        QPair<double, double> pa = ctx.point(a);
        for (int b = bucketStart; b < bucketEnd; ++b) {
            QPair<double, double> pb = ctx.point(b);
            double area = std::abs(
                (pa.first * (pb.second - avgY)) +
                (pb.first * (avgY - pa.second)) +
                (avgX * (pa.second - pb.second))
            ) * 0.5;
            if (area > maxArea) {
                maxArea = area;
                maxAreaIndex = b;
            }
        }
        ctx.append(maxAreaIndex);
        a = maxAreaIndex;
    }
    ctx.append(rightLogic);                      // 最后一个点
}

void DataStorage::Private::_m4DownSample(DownsampleCtx &ctx, int startIndex, int leftLogic, int rightLogic, int threshold)
{
    const int pointCnt = rightLogic - leftLogic + 1;

    ctx.append(leftLogic);                       // 首点强制保留
    int bucketCount = qMax(1, threshold / 2);
    int bucketSize  = (pointCnt + bucketCount - 1) / bucketCount;

    for (int start = leftLogic; start <= rightLogic; start += bucketSize) {
        int end = qMin(start + bucketSize - 1, rightLogic);
        int minIdx = start, maxIdx = start;
        double minY = ctx.yValue(start);
        double maxY = minY;
        for (int i = start + 1; i <= end; ++i) {
            double y = ctx.yValue(i);
            if (y < minY) { minY = y; minIdx = i; }
            if (y > maxY) { maxY = y; maxIdx = i; }
        }
        // 桶内极值按 x 顺序追加（避免折返锯齿），跳过首尾点（已单独保留）
        const int firstIdx  = qMin(minIdx, maxIdx);
        const int secondIdx = qMax(minIdx, maxIdx);
        if (firstIdx != leftLogic && firstIdx != rightLogic)
            ctx.append(firstIdx);
        if (secondIdx != firstIdx && secondIdx != leftLogic && secondIdx != rightLogic)
            ctx.append(secondIdx);
    }
    ctx.append(rightLogic);                      // 末点强制保留
}


// 二分法逻辑索引查找，可用于环形缓冲区（逻辑单调递增）
static int _binarySearchLogic(const QList<double>& num, int len, int startIndex, double target, bool isBig)
{
    // len : 待查找数据长度。startIndex : 逻辑起点对应的物理索引。
    if (len <= 0 || num.isEmpty() || startIndex < 0)  return -1;     // 判空
    int left = 0, right = len - 1;
    int ans = -1;

    while (left <= right) {
        int mid = (left + right) >> 1;      // 移位，性能更好
        int pos = startIndex + mid;         // 此处可以用减法代替取模，性能更优
        int realIdx = (pos >= len) ? (pos - len) : pos;
        if (isBig) {
            if (num[realIdx] >= target) {
                ans   = mid;
                right = mid - 1;
            } 
            else    left = mid + 1;
        } else {
            if (num[realIdx] <= target) {
                ans  = mid;
                left = mid + 1;
            } 
            else    right = mid - 1;
        }
    }
    return ans;         // 返回逻辑索引值：[0, len)
}

// ---------- DataStorage 公共接口实现 ----------
DataStorage::DataStorage(int maxPoints, int channelCount, QObject *parent)
    : QObject(parent), pimpl(new Private(maxPoints, channelCount, this)) {}
DataStorage::~DataStorage() {}

// add(ch, value) 接口内部自动生成时间戳
void DataStorage::addData(int ch, double value)  { pimpl->addData(ch, -1.0, value); } 
void DataStorage::addData(int ch, double time, double value)   { pimpl->addData(ch, time, value); }
void DataStorage::addData(const QList<ChannelData>& dataLists) { pimpl->addData(dataLists); }
void DataStorage::resetData(int ch)                            { pimpl->resetData(ch); }
qint64 DataStorage::dataVersion() const                        { return pimpl->dataVersion(); }
// Qt6 QList的隐式共享机制保证了返回值开销足够低
ChannelData DataStorage::getData(int ch, int lastN)            { return pimpl->getData(ch, lastN); }

ChannelData DataStorage::getData(int ch, double startTime, double endTime)
{
    return pimpl->getData(ch, startTime, endTime, -1, false);     // 不启用降采样  
}

ChannelData DataStorage::getData(int ch, double startTime, double endTime, int threshold, bool isLTTB)
{
    return pimpl->getData(ch, startTime, endTime, threshold, isLTTB);
}

#include "data_storage.moc"