#ifndef __DATA_STORAGE_H__
#define __DATA_STORAGE_H__

#include <QObject>
#include <QMutex>
#include <QLineSeries>
#include <QtMath>
#include <limits>

typedef struct{
    QList<float> target;      //目标值
    QList<float> actual;      //实际值
    QList<qint64> timestamp;  //时间戳
    int head;                   //环形缓冲区写指针
    int count;                  //当前有效数据点数
}channel_data_t;

class DataStorage : public QObject{
    Q_OBJECT
public:
    DataStorage(int maxPoints = 720000);    //默认1h, 5ms
    void addData(int channel, float target, float actual);
    void addData(int channel, float target, float actual, qint64 time);
    void getData(int ch, qint64 startTime, qint64 endTime, QList<float>& targetOut, QList<float>& actualOut, QList<qint64>& timeOut);
    void fillSeries(int ch, qint64 viewStart, qint64 viewEnd, QLineSeries* targetSeries, QLineSeries* actualSeries, bool isAbs);
    void fillSeriesLTTB(int ch, qint64 startTime, qint64 endTime, QLineSeries* targetSeries, QLineSeries* actualSeries, bool isAbs, int threshold);
    void getTimeRange(qint64& startTime, qint64& endTime);      //获取数据的时间戳范围
    void setBaseTimestamp(qint64 baseTime);                     //设置基准时间戳。
    qint64 getBaseTimeStamp() const     {   return m_baseTimestamp; }
private:
    void _fillSeries(int ch, qint64 viewStart, qint64 viewEnd, QLineSeries* targetSeries, QLineSeries* actualSeries, bool isAbs, int lttb_threshold);
    int _getData(int ch, qint64 startTime, qint64 windowMs, int& startIndex, int& leftLogic, int& rightLogic);
    void _addData(int ch, float target, float actual, qint64 time);
    QPointF _getPoint(int ch, int index, qint64 now, bool isTarget, bool isAbs);
private:
    QList<channel_data_t> m_channels;
    int m_max_points;  
    QMutex m_mutex;
    qint64 m_baseTimestamp;   // 基准时间戳（毫秒）
};

#endif