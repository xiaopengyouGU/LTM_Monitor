#ifndef DATA_STORAGE_H__
#define DATA_STORAGE_H__

#include <QObject>

#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif

struct ChannelData;

class CHART_EXPORT DataStorage : public QObject
{
    Q_OBJECT
public:
    // maxPoints：每个通道最大存储点数，channelCount：通道数（1~60）
    explicit DataStorage(int maxPoints = 720000, int channelCount = 10, QObject *parent = nullptr);
    ~DataStorage();
    // 数据时间戳必须单调递增！！！
    void addData(int channel, double value);               // 添加数据（使用内部时间戳）
    void addData(int channel, double time, double value);  // 添加数据（指定时间戳）
    void addData(const QList<ChannelData>& dataList);      // 直接添加通道数据
    void resetData(int channel);                           // 清空通道数据   
    qint64 dataVersion() const;                            // 数据版本号（写入/清空递增），刷新去重用
    ChannelData  getData(int channel, int lastN);          // 获取最近的 N 个 数据
    // 获取指定时间范围内的数据（startTime < 0, endTime < 0时，返回全部数据）
    ChannelData  getData(int channel, double startTime, double endTime);
    //获取 降采样处理后的原始数据（指针），减少拷贝开销，支持 M4 和 LTTB 降采样
    //注意：同一通道同一时刻只允许一个调用方（内部写共享降采样缓冲）
    ChannelData* getData(int channel, double startTime, double endTime, int threshold, bool isLTTB);

private:
    class Private;
    Private *pimpl;                 // 私有实现指针，由 Qt 对象树管理
};

#endif // DATA_STORAGE_H__