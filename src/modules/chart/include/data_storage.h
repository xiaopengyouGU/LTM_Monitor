#ifndef DATA_STORAGE_H__
#define DATA_STORAGE_H__

#include <QObject>
#include <QLineSeries>
#include <QList>
#include <QMutex>

#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif

class ChannelData;

class CHART_EXPORT DataStorage : public QObject
{
    Q_OBJECT
public:
    // 构造函数：maxPoints 为每个通道最大存储点数，channelCount 为通道数（1~10）
    explicit DataStorage(int maxPoints = 720000, int channelCount = 5, QObject *parent = nullptr);
    ~DataStorage();

    void addData(int channel, float target, float actual);                     // 添加数据（使用当前时间戳）
    void addData(int channel, float target, float actual, qint64 timestamp);   // 添加数据（指定时间戳）
    void addData(const QList<ChannelData>& dataNum);                           // 直接添加通道数据,采用Qt隐式共享机制
    // 获取指定时间范围内的原始数据
    void getData(int channel, qint64 startTime, qint64 endTime, QList<float>& targetOut, QList<float>& actualOut, QList<qint64>& timeOut);
    //获取处理后的数据点数组，用于更新曲线序列，效率超高
    QList<QPointF>* getPointNum(int ch, qint64 viewStart, qint64 viewEnd, bool isAbs, bool isTarget, int threshold, bool useLTTB);
    // 获取所有数据的时间范围
    void getTimeRange(qint64& startTime, qint64& endTime) const;
    void setBaseTimestamp(qint64 baseTime);                                    // 设置基准时间戳（用于绝对时间显示）
    qint64 getBaseTimestamp() const;                                           // 获取基准时间戳
    int  getChannelCount()  const;                                             // 返回存储的通道数量

private:
    class Private;
    Private *pimpl;                 // 私有实现指针，由 Qt 对象树管理
};

#endif // DATA_STORAGE_H__