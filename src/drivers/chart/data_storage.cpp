#include "data_storage.h"
#include <QDateTime>
#include <cmath>

template<typename GetPointFunc>
static void lttbDownsample(GetPointFunc getPoint, int firstLogic, int lastLogic, int threshold, QList<QPointF>& outPoints);
static int _binarySearchLogic(const QList<qint64>& num, int len, int startIndex, qint64 target, bool isBig);

DataStorage::DataStorage(int maxPoint):m_max_points(maxPoint)
{
    m_baseTimestamp = QDateTime::currentMSecsSinceEpoch();  //基准时间戳
    m_channels.resize(5);   // 预分配内存，确保有5个通道                
}

void DataStorage::addData(int ch, float target, float actual)
{
    if(ch < 0 || ch >= 5 )  return;
    QMutexLocker locker(&m_mutex);
    qint64 time = QDateTime::currentMSecsSinceEpoch();
    _addData(ch, target, actual, time);
}

void DataStorage::addData(int ch, float target, float actual, qint64 time)
{
    if(ch < 0 || ch >= 5 )  return;
    QMutexLocker locker(&m_mutex);
    _addData(ch, target, actual, time);
}

void DataStorage::getData(int ch, qint64 startTime, qint64 endTime, QList<float>& targetOut, QList<float>& actualOut, QList<qint64>& timeOut)
{  
    QMutexLocker locker(&m_mutex);
    int startIndex, maxPoints = m_max_points;
    int leftLogic, rightLogic;
    int pointCnt = _getData(ch, startTime, endTime, startIndex, leftLogic, rightLogic);
    if(pointCnt == 0)   return;             //数据获取失败
    const channel_data_t& cd = m_channels[ch];
    //先清空数组,并分配内存
    targetOut.clear();
    actualOut.clear();
    timeOut.clear();
    targetOut.reserve(pointCnt);
    actualOut.reserve(pointCnt);
    timeOut.reserve(pointCnt);
    
    for(int logic = leftLogic; logic <= rightLogic; logic ++)
    {
        int realIdx = (startIndex + logic) % maxPoints;
        targetOut.append(cd.target[realIdx]);
        actualOut.append(cd.actual[realIdx]);
        timeOut.append(cd.timestamp[realIdx]);
    }
}

void DataStorage::fillSeries(int ch, qint64 startTime, qint64 endTime, QLineSeries* targetSeries, QLineSeries* actualSeries, bool isAbs) 
{
    QMutexLocker locker(&m_mutex);      //避免线程干涉，锁在整个函数执行完毕后释放
    _fillSeries(ch, startTime, endTime, targetSeries, actualSeries, isAbs, 0);
}

//模式数据填充，LTTB筛点算法，可显示数据跃变和细节
// -------------------- fillSeriesLTTB 实现 --------------------
void DataStorage::fillSeriesLTTB(int ch, qint64 startTime, qint64 endTime, QLineSeries* targetSeries, QLineSeries* actualSeries, bool isAbs, int threshold)
{
    QMutexLocker locker(&m_mutex);      //避免线程干涉，锁在整个函数执行完毕后释放
    _fillSeries(ch, startTime, endTime, targetSeries, actualSeries, isAbs, threshold);
}

void DataStorage::getTimeRange(qint64& startTime, qint64& endTime)         //获取数据的时间戳范围
{
    QMutexLocker locker(&m_mutex);      //避免线程干涉，锁在整个函数执行完毕后释放
    startTime = std::numeric_limits<qint64>::max();
    endTime = std::numeric_limits<qint64>::lowest();

    for(int ch = 0; ch < 5; ch++)
    {
        const channel_data_t& cd = m_channels[ch];
        int maxPoints = m_max_points;
        int head = cd.head;
        int count = cd.count;
        if(count == 0) continue;
        int startIndex = (count < maxPoints) ? 0 : head;        // 逻辑起点对应的物理索引
        int endIndex = (startIndex + count - 1) % maxPoints;    //逻辑终点对应的物理索引
        if(startTime > cd.timestamp[startIndex])    startTime = cd.timestamp[startIndex];
        if(endTime < cd.timestamp[endIndex])        endTime = cd.timestamp[endIndex];
    }
}

void DataStorage::setBaseTimestamp(qint64 baseTime)           //设置基准时间戳。
{
    m_baseTimestamp = baseTime;
}
/********************************************************************/
/* 静态函数 */
//二分查找, 适用于环形缓冲区（"单调递增"）
// 返回逻辑偏移量，找不到返回 -1
int DataStorage::_getData(int ch, qint64 startTime, qint64 endTime, int& startIndex, int& leftLogic, int& rightLogic)
{
    if (ch < 0 || ch >= 5)  return 0;
    const channel_data_t& cd = m_channels[ch];
    if (cd.count == 0)  return 0;

    int maxPoints = m_max_points;
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
    return pointCnt;                    //放回最终数据点数
}

void DataStorage::_addData(int ch, float target, float actual, qint64 time)
{
    channel_data_t& cd = m_channels[ch];
    int index = cd.head;
    if(cd.count == m_max_points)            //把环形缓冲区填满了,这时候说明所有内存都分配完毕
    {
        cd.target[index] = target;
        cd.actual[index] = actual;
        cd.timestamp[index] = time;
    }
    else                                    //环形缓冲区暂时没有填满，直接用下标访问会报错。
    {
        cd.target.append(target);
        cd.actual.append(actual);
        cd.timestamp.append(time);          //保留时间戳，用于记录历史数据和同步
    }
    cd.head = (cd.head + 1) % m_max_points;
    if(cd.count < m_max_points) cd.count++; 
}

//填充序列数据
void DataStorage::_fillSeries(int ch,qint64 viewStart, qint64 viewEnd, QLineSeries* targetSeries, QLineSeries* actualSeries, bool isAbs, int lttb_threshold)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();   //记录当前时间戳
    int startIndex, maxPoints = m_max_points;
    int leftLogic, rightLogic;
    int pointCnt = _getData(ch, viewStart, viewEnd, startIndex, leftLogic, rightLogic); //now是当前时间戳
    if(pointCnt == 0)   return;          //数据获取失败
    const channel_data_t& cd = m_channels[ch]; 
    QList<QPointF> targetPoints, actualPoints;
    
    if(lttb_threshold == 0)             //自动模式下,显示时间固定，不采用LTTB降采样算法
    {                                   
        targetPoints.reserve(pointCnt); //此时才预分配内容，保证性能开销
        actualPoints.reserve(pointCnt);
        for (int logic = leftLogic; logic <= rightLogic; ++logic) 
        {
            int realIdx = (startIndex + logic) % maxPoints;
            targetPoints.append(_getPoint(ch, realIdx, now, true, isAbs));
            actualPoints.append(_getPoint(ch, realIdx, now, false, isAbs));
        }
    }
    else                                //手动模式采用LTTB降采样算法
    {
        auto getTargetPoint = [&](int logic) {
            int realIdx = (startIndex + logic) % maxPoints;
            return _getPoint(ch, realIdx, now, true, isAbs);
        };
        auto getActualPoint = [&](int logic) {
            int realIdx = (startIndex + logic) % maxPoints;
            return _getPoint(ch, realIdx, now, false, isAbs);
        };
        lttbDownsample(getTargetPoint, leftLogic, rightLogic, lttb_threshold, targetPoints);
        lttbDownsample(getActualPoint, leftLogic, rightLogic, lttb_threshold, actualPoints);
    };

    targetSeries->replace(targetPoints);
    actualSeries->replace(actualPoints);
}

QPointF DataStorage::_getPoint(int ch, int index, qint64 now, bool isTarget, bool isAbs)
{
    qreal sec;
    const channel_data_t& cd = m_channels[ch];
    
    if(isAbs)   //绝对时间轴
        sec = (cd.timestamp[index] - m_baseTimestamp) / 1000.0;
    else        //相对时间轴
        sec = (cd.timestamp[index] - now) / 1000.0;
    qreal val = isTarget ? cd.target[index] : cd.actual[index];
    return QPointF(sec, val);
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
static void lttbDownsample(GetPointFunc getPoint, int firstLogic, int lastLogic, int threshold, QList<QPointF>& outPoints)
{
    outPoints.clear();
    int pointCnt = lastLogic - firstLogic + 1;
    if(pointCnt <= threshold) {         //区间点数未到阈值，按pointCnt分配内存，最后直接返回，不进行LTTB采样
        outPoints.reserve(pointCnt);
        for (int logic = firstLogic; logic <= lastLogic; ++logic) {
            outPoints.append(getPoint(logic));
        }
        return;
    }
    //区间点数超过阈值，启动LTTB采样
    outPoints.reserve(threshold);
    outPoints.append(getPoint(firstLogic)); //第一个点

    double bucketSize = (pointCnt - 2.0) / (threshold - 2.0);
    int a = firstLogic;
    for (int i = 0; i < threshold - 2; ++i) {
        int bucketStart = std::max(a + 1, firstLogic + (int)std::floor((i + 1) * bucketSize));
        int bucketEnd   = std::min(lastLogic, firstLogic + (int)std::floor((i + 2) * bucketSize));
        if (bucketStart >= bucketEnd) {
            outPoints.append(getPoint(bucketStart));
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
        outPoints.append(getPoint(maxAreaIndex));
        a = maxAreaIndex;
    }
    outPoints.append(getPoint(lastLogic));                  //也要最后一个点
}