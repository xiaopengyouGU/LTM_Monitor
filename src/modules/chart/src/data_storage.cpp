#include "data_storage.h"
#include "chart_def.h"
#include "chart_manager.h"
#include <QDateTime>
#include <QtMath>
#include <limits>
#include <QReadWriteLock>     //读写锁

//LTTB降采样算法，用于数据分析（updateAll， 全量数据更新，不会自动更新图表）
template<typename GetPointFunc>
static void lttbDownsample(GetPointFunc getPoint, int firstLogic, int lastLogic, QList<QPointF>* outPoints, int threshold);
//M4降采样算法，精度比LTTB略低，但效果也很好，适合实时显示。
template<typename GetPointFunc>
static void m4Downsample(GetPointFunc getPoint, int firstLogic, int lastLogic, QList<QPointF>* outPoints, int threshold);
//二进制逻辑查找
static int _binarySearchLogic(const QList<qint64>& num, int len, int startIndex, qint64 target, bool isBig);
// ---------- 私有实现类 ----------
class DataStorage::Private : public QObject
{
    Q_OBJECT
public:
    Private(int maxPoints, int channelCount, DataStorage *parent);
    ~Private();

    // 公开给 DataStorage 调用的函数
    void addData(int ch, float target, float actual, qint64 timestamp);
    void addData(const QList<ChannelData>& dataNum);
    void getData(int ch, qint64 startTime, qint64 endTime, QList<float>& targetOut, QList<float>& actualOut, QList<qint64>& timeOut);
    //获取处理后的数据点数组，用于更新曲线序列，效率超高
    QList<QPointF>* getPointNum(int ch, qint64 viewStart, qint64 viewEnd, bool isAbs, bool isTarget, int threshold);
    QList<QPointF>* getPointNumLTTB(int ch, qint64 viewStart, qint64 viewEnd, bool isAbs, bool isTarget, int threshold);
    void getTimeRange(qint64& startTime, qint64& endTime) const;
    void setBaseTimestamp(qint64 baseTime)  { m_baseTimestamp = baseTime; }
    qint64 getBaseTimestamp()   const       { return m_baseTimestamp; }
    int  getChannelCount()      const       { return m_channelCount; }
private:
    // 内部辅助函数
    void _addData(int ch, float target, float actual, qint64 timestamp);
    int _getPointCnt(int ch, qint64 startTime, qint64 endTime,
                 int& startIndex, int& leftLogic, int& rightLogic) const;
    QList<QPointF>* _getPointNum(int ch, qint64 viewStart, qint64 viewEnd, bool isAbs, 
                                bool isTarget, int threshold, bool useLTTB = false);
    QPointF _getPoint(int channel, int index, qint64 now, bool isTarget, bool isAbs) const;

    int m_maxPoints;                    // 每个通道最大存储点数
    int m_channelCount;                 // 实际通道数（1~10）
    QList<ChannelData> m_channels;      // 每个通道的数据结构
    QList<QList<QPointF>> m_target_pointNums;   // 原始数据处理后的得到的点列表，这个列表始终存在。
    QList<QList<QPointF>> m_actual_pointNums;   //
    mutable QReadWriteLock m_rwLock;            // 读写锁，读可以并发，写不行
    qint64 m_baseTimestamp;                     // 基准时间戳
};

// ---------- Private 实现 ----------
DataStorage::Private::Private(int maxPoints, int channelCount, DataStorage *parent)
    : QObject(parent), m_maxPoints(maxPoints), m_channelCount(channelCount)
{
    // 通道数限制 1~10，默认 5
    if (m_channelCount < 1 || m_channelCount > MAX_CHANNEL_NUMS)
        m_channelCount = DEFUALT_CHANNEL_NUMS;
    //通道点数设置，有最大点数限制
    if(maxPoints > MAX_CHANNEL_POINTS )         maxPoints = MAX_CHANNEL_POINTS;
    else if(maxPoints < MIN_CHANNEL_POINTS)     maxPoints = MIN_CHANNEL_POINTS;
    // 初始化每个通道的环形缓冲区
    m_channels.resize(m_channelCount);
    for (int i = 0; i < m_channelCount; ++i) 
    {
        m_channels[i].target.resize(m_maxPoints);       //直接分配好内存
        m_channels[i].actual.resize(m_maxPoints);       //可以直接访问下标
        m_channels[i].timestamp.resize(m_maxPoints);
        m_channels[i].head = 0;
        m_channels[i].count = 0;
        //初始化用于存储每个通道处理后数据点的数组
        m_target_pointNums.append(QList<QPointF>());
        m_actual_pointNums.append(QList<QPointF>());
    }
    m_baseTimestamp = -1;                               //基准时间戳，-1表示还没有接收到数据
}

DataStorage::Private::~Private()
{
    // 无需手动释放，父对象会自动销毁
}

void DataStorage::Private::_addData(int channel, float target, float actual, qint64 timestamp)
{   //已经提前分配好内存，可以直接下标访问，已经全部初始化为0了
    ChannelData &cd = m_channels[channel];
    int index = cd.head;
    cd.target[index] = target;
    cd.actual[index] = actual;
    cd.timestamp[index] = timestamp;
    cd.head = (cd.head + 1) % m_maxPoints;
    if (cd.count < m_maxPoints) cd.count++;
}

void DataStorage::Private::addData(int channel, float target, float actual, qint64 timestamp)
{
    if (channel < 0 || channel >= m_channelCount) return;
    QWriteLocker locker(&m_rwLock);   // 写锁
    if(m_baseTimestamp == -1)   m_baseTimestamp = timestamp;  //说明之前还没有添加数据，现在更新基准时间戳  
    _addData(channel, target, actual, timestamp);
}

void DataStorage::Private::addData(const QList<ChannelData>& dataNum)
{
    QWriteLocker locker(&m_rwLock);   // 写锁
    //检查数据格式是否正确，约定传入的数据没有环形缓冲区
    int size = dataNum.size();
    int len = qMin(m_channelCount, size);   //检查数据范围
    for(int i = 0; i < len; i++)
    {
        const ChannelData& cd = dataNum[i];
        if(cd.actual.size() != cd.target.size() 
            || cd.actual.size() != cd.timestamp.size()
            || cd.target.size() != cd.timestamp.size())   
            continue;       //传入的数据格式不对,跳过
        
        if(!cd.actual.size())   continue;                               //必须要更新基准时间戳，否则会出大问题。
        if(m_baseTimestamp == -1)   m_baseTimestamp = cd.timestamp[0];  //说明之前还没有添加数据，现在更新基准时间戳  
        
        for(int j = 0; j < cd.actual.size(); j++)
        {   //添加数据
            _addData(i, cd.target[j], cd.actual[j], cd.timestamp[j]);
        }
    }
    //数据添加完毕后，直接释放锁
}   
//利用二分查找快速返回目标访问的点数
int DataStorage::Private::_getPointCnt(int ch, qint64 startTime, qint64 endTime,
                                   int& startIndex, int& leftLogic, int& rightLogic) const
{
    if(ch < 0 || ch >= m_channelCount)  return 0;
    const ChannelData& cd = m_channels[ch];
    if (cd.count == 0)  return 0;

    int maxPoints = m_maxPoints;
    int head = cd.head;
    int count = cd.count;
    startIndex = (count < maxPoints) ? 0 : head;   // 逻辑起点对应的物理索引
    int len = count;

    qint64 earliest = cd.timestamp[startIndex];
    qint64 latest = cd.timestamp[(startIndex + len - 1) % maxPoints];
    //若输入时间<= 0， 默认取所有数据
    if(endTime <= 0) endTime = latest;
    if(startTime <= 0) startTime = earliest;
    //时间范围判别
    if (endTime < earliest || startTime > latest) return 0;
    if (startTime < earliest) startTime = earliest;    //调整实际时间，
    if (endTime > latest) endTime = latest;

    leftLogic = _binarySearchLogic(cd.timestamp, len, startIndex, startTime, true);
    rightLogic = _binarySearchLogic(cd.timestamp, len, startIndex, endTime, false);
    //没找着正常的逻辑偏移值
    if (leftLogic == -1 || rightLogic == -1 || leftLogic > rightLogic) return 0;
    int pointCnt = rightLogic - leftLogic + 1;
    return pointCnt;                    //返回最终数据点数
}

QPointF DataStorage::Private::_getPoint(int channel, int index, qint64 now, bool isTarget, bool isAbs) const
{
    const ChannelData &cd = m_channels[channel];
    qreal sec;
    if (isAbs)
        sec = (cd.timestamp[index] - m_baseTimestamp) / 1000.0;
    else
        sec = (cd.timestamp[index] - now) / 1000.0;
    qreal val = isTarget ? cd.target[index] : cd.actual[index];
    return QPointF(sec, val);
}

QList<QPointF>* DataStorage::Private::_getPointNum(int ch, qint64 viewStart, qint64 viewEnd,
                                                    bool isAbs, bool isTarget, int threshold,
                                                    bool useLTTB)
{   // 使用读锁，允许多个读操作并发，提高并行性能
    QReadLocker locker(&m_rwLock);    //暂时注释掉锁

    // 1. 获取时间范围内数据的逻辑索引范围
    int startIndex, leftLogic, rightLogic;
    int pointCnt = _getPointCnt(ch, viewStart, viewEnd, startIndex, leftLogic, rightLogic);
    if (pointCnt == 0) return nullptr;   // 无数据，返回空

    // 2. 获取该通道对应的点列表指针（目标值或实际值），并清空旧数据
    QList<QPointF>* pointNum = isTarget ? &m_target_pointNums[ch] : &m_actual_pointNums[ch];
    pointNum->clear();                  // 清空原始数据

    // 3. 准备访问原始环形缓冲区所需的变量
    const ChannelData& cd = m_channels[ch];
    int maxPoints = m_maxPoints;
    qint64 now = QDateTime::currentMSecsSinceEpoch();   // 当前时间戳（用于相对时间计算）
    qint64 base = m_baseTimestamp;                      // 基准时间戳（用于绝对时间计算）

    int firstLogic = leftLogic;
    int lastLogic = rightLogic;

    // 4. 定义 lambda，通过逻辑索引直接获取环形缓冲区中的点（无需拷贝）
    auto getPoint = [&](int logic) -> QPointF {
        int realIdx = (startIndex + logic) % maxPoints;                 // 物理索引
        qreal val = isTarget ? cd.target[realIdx] : cd.actual[realIdx]; // 数值
        // 计算 X 轴时间（秒）
        qreal sec = isAbs ? (cd.timestamp[realIdx] - base) / 1000.0
                          : (cd.timestamp[realIdx] - now) / 1000.0;
        return QPointF(sec, val);
    };

    // 5. 根据点数与阈值的关系选择处理方式
    if (pointCnt <= threshold) {
        // 点数未超过阈值：直接输出所有点（无需降采样）
        pointNum->reserve(pointCnt);                //预分配内存
        for (int logic = firstLogic; logic <= lastLogic; ++logic) {
            pointNum->append(getPoint(logic));
        }
    } else {
        // 点数超过阈值，需要降采样
        pointNum->reserve(threshold);               //预分配内存
        if (useLTTB) {      // 使用 LTTB 算法（适用于高精度分析，如 updateAll）
            lttbDownsample(getPoint, firstLogic, lastLogic, pointNum, threshold);
        } else {        //采用M4降采样算法绘制
            m4Downsample(getPoint, firstLogic, lastLogic, pointNum, threshold);
        }
    }

    return pointNum;   // 返回处理后的点列表指针
}

QList<QPointF>* DataStorage::Private::getPointNum(int ch, qint64 viewStart, qint64 viewEnd,
                                        bool isAbs, bool isTarget, int threshold)
{
    //耗时操作，锁要加在内部的拷贝处,不采用LTTB算法
    return _getPointNum(ch, viewStart, viewEnd, isAbs, isTarget, threshold);
}

QList<QPointF>* DataStorage::Private::getPointNumLTTB(int ch, qint64 viewStart, qint64 viewEnd,
                                                     bool isAbs, bool isTarget,int threshold)
{
    //耗时操作，锁要加在内部的拷贝处
    return _getPointNum(ch, viewStart, viewEnd, isAbs, isTarget, threshold, true);
}

void DataStorage::Private::getData(int channel, qint64 startTime, qint64 endTime,
                                   QList<float>& targetOut, QList<float>& actualOut, QList<qint64>& timeOut)
{
    QReadLocker locker(&m_rwLock);;    //加锁，避免读取时出现数据覆盖问题
    int startIndex, leftLogic, rightLogic;
    int pointCnt = _getPointCnt(channel, startTime, endTime, startIndex, leftLogic, rightLogic);
    if (pointCnt == 0) return;

    const ChannelData &cd = m_channels[channel];
    int maxPoints = m_maxPoints;
    targetOut.clear();
    actualOut.clear();
    timeOut.clear();
    targetOut.reserve(pointCnt);
    actualOut.reserve(pointCnt);
    timeOut.reserve(pointCnt);

    for (int logic = leftLogic; logic <= rightLogic; ++logic) 
    {
        int realIdx = (startIndex + logic) % maxPoints;
        targetOut.append(cd.target[realIdx]);
        actualOut.append(cd.actual[realIdx]);
        timeOut.append(cd.timestamp[realIdx]);
    }
}

void DataStorage::Private::getTimeRange(qint64& startTime, qint64& endTime) const
{
    QReadLocker locker(&m_rwLock);              //读锁，读可以并发
    startTime = std::numeric_limits<qint64>::max();
    endTime = std::numeric_limits<qint64>::lowest();

    for (int ch = 0; ch < m_channelCount; ++ch) {
        const ChannelData &cd = m_channels[ch];
        if (cd.count == 0) continue;
        int head = cd.head;
        int count = cd.count;
        int startIndex = (count < m_maxPoints) ? 0 : head;
        int endIndex = (startIndex + count - 1) % m_maxPoints;
        if (cd.timestamp[startIndex] < startTime) startTime = cd.timestamp[startIndex];
        if (cd.timestamp[endIndex] > endTime) endTime = cd.timestamp[endIndex];
    }
}

static int _binarySearchLogic(const QList<qint64>& num, int len, int startIndex, qint64 target, bool isBig)
{
    int left = 0, right = len - 1;
    int ans = -1;
    while (left <= right) 
    {
        int mid = (left + right) / 2;
        int realIdx = (startIndex + mid) % len;
        if (isBig) 
        {
            if (num[realIdx] >= target) 
            {
                ans = mid;
                right = mid - 1;
            } 
            else    left = mid + 1;
        } 
        else
        {
            if (num[realIdx] <= target)
            {
                ans = mid;
                left = mid + 1;
            } 
            else    right = mid - 1;
        }
    }
    return ans;
}

template<typename GetPointFunc>
static void lttbDownsample(GetPointFunc getPoint, int firstLogic, int lastLogic, QList<QPointF>* outPoints, int threshold)
{
    int pointCnt = lastLogic - firstLogic + 1;

    outPoints->append(getPoint(firstLogic)); //第一个点
    double bucketSize = (pointCnt - 2.0) / (threshold - 2.0);
    int a = firstLogic;
    for (int i = 0; i < threshold - 2; ++i) {
        int bucketStart = std::max(a + 1, firstLogic + (int)std::floor((i + 1) * bucketSize));
        int bucketEnd   = std::min(lastLogic, firstLogic + (int)std::floor((i + 2) * bucketSize));
        if (bucketStart >= bucketEnd) {
            outPoints->append(getPoint(bucketStart));
            a = bucketStart;
            continue;
        }
        // 计算下一个桶的平均点
        int avgRangeStart = bucketEnd;
        int avgRangeEnd   = (i == threshold - 3) ? lastLogic : firstLogic + (int)std::floor((i + 3) * bucketSize);
        avgRangeEnd = std::min(avgRangeEnd, lastLogic);
        double avgX = 0, avgY = 0;
        int avgCount = avgRangeEnd - avgRangeStart + 1;
        if (avgCount > 0) {
            for (int k = avgRangeStart; k <= avgRangeEnd; ++k) {
                QPointF p = getPoint(k);
                avgX += p.x();
                avgY += p.y();
            }
            avgX /= avgCount;
            avgY /= avgCount;
        } else {
            QPointF p = getPoint(avgRangeStart);
            avgX = p.x();
            avgY = p.y();
        }

        double maxArea = -1;
        int maxAreaIndex = bucketStart;
        QPointF p_a = getPoint(a);
        for (int b = bucketStart; b < bucketEnd; ++b) {
            QPointF p_b = getPoint(b);
            double area = std::abs(
                (p_a.x() * (p_b.y() - avgY)) +
                (p_b.x() * (avgY - p_a.y())) +
                (avgX * (p_a.y() - p_b.y()))
            ) * 0.5;
            if (area > maxArea) {
                maxArea = area;
                maxAreaIndex = b;
            }
        }
        outPoints->append(getPoint(maxAreaIndex));
        a = maxAreaIndex;
    }
    outPoints->append(getPoint(lastLogic));                  //也要最后一个点
}

template<typename GetPointFunc>
static void m4Downsample(GetPointFunc getPoint, int firstLogic, int lastLogic, QList<QPointF>* outPoints, int threshold)
{
    int pointCnt = lastLogic - firstLogic + 1;
    // 目标输出点数接近 threshold，每个桶最多输出 2 个点
    int bucketCount = qMax(1, threshold / 2);
    int bucketSize = (pointCnt + bucketCount - 1) / bucketCount; // 向上取整

    for (int start = firstLogic; start <= lastLogic; start += bucketSize) {
        int end = qMin(start + bucketSize - 1, lastLogic);

        // 初始化桶内第一个点作为最小/最大候选
        int minIdx = start, maxIdx = start;
        QPointF minPoint = getPoint(start);
        QPointF maxPoint = minPoint;

        // 遍历桶内剩余点，找出最小值和最大值
        for (int i = start + 1; i <= end; ++i) {
            QPointF p = getPoint(i);
            if (p.y() < minPoint.y()) {
                minPoint = p;
                minIdx = i;
            }
            if (p.y() > maxPoint.y()) {
                maxPoint = p;
                maxIdx = i;
            }
        }
        // 输出最小点（必须）
        outPoints->append(minPoint);
        // 如果最小点和最大点不同，再输出最大点
        if (minIdx != maxIdx) {
            outPoints->append(maxPoint);
        }
    }
}

// ---------- DataStorage 公共接口实现 ----------
DataStorage::DataStorage(int maxPoints, int channelCount, QObject *parent)
    : QObject(parent), pimpl(new Private(maxPoints, channelCount, this)){}

qint64 DataStorage::getBaseTimestamp() const  { return pimpl->getBaseTimestamp();}

DataStorage::~DataStorage()
{
    // pimpl 是 QObject 的子对象，会被 Qt 对象树自动删除，无需手动 delete
}

void DataStorage::addData(int channel, float target, float actual)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    pimpl->addData(channel, target, actual, now);
}

void DataStorage::addData(int channel, float target, float actual, qint64 timestamp)
{
    pimpl->addData(channel, target, actual, timestamp);
}

void DataStorage::addData(const QList<ChannelData>& dataNum)
{   //dataNum传递过程采用隐式共享，只读的情况下（同一线程）只进行引用计数，不做深拷贝
    pimpl->addData(dataNum);
}

void DataStorage::getData(int channel, qint64 startTime, qint64 endTime,
                          QList<float>& targetOut, QList<float>& actualOut, QList<qint64>& timeOut)
{
    pimpl->getData(channel, startTime, endTime, targetOut, actualOut, timeOut);
}

QList<QPointF>* DataStorage::getPointNum(int ch, qint64 viewStart, qint64 viewEnd,
                                        bool isAbs, bool isTarget, int threshold, bool useLTTB)
{   if(useLTTB)
            return pimpl->getPointNumLTTB(ch, viewStart, viewEnd, isAbs, isTarget, threshold);
    else    return pimpl->getPointNum(ch, viewStart, viewEnd, isAbs, isTarget, threshold);
}

void DataStorage::getTimeRange(qint64& startTime, qint64& endTime) const
{
    pimpl->getTimeRange(startTime, endTime);
}

void DataStorage::setBaseTimestamp(qint64 baseTime) { pimpl->setBaseTimestamp(baseTime);}

int DataStorage::getChannelCount() const { return pimpl->getChannelCount(); }

#include "data_storage.moc"