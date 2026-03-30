#ifndef CHART_MANAGER_H__
#define CHART_MANAGER_H__

#include <QObject>
#include <QString>
#include <QChart>

// 为动态库添加导出宏！！！
// chart_manager.h
#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif

typedef enum{
    Color_White = 0,
    Color_Black,                    
}WindowColor;

typedef enum{
    Mode_Auto = 0,                  //自动模式
    Mode_Hand,                      //手动模式,1
}ShowMode;

// 环形缓冲区数据结构（也是通道数据）
class ChannelData {
public:
    QList<float> target;      // 目标值列表
    QList<float> actual;      // 实际值列表
    QList<qint64> timestamp;  // 时间戳列表
    int head;                 // 环形缓冲区写指针
    int count;                // 当前有效数据点数
    ChannelData() : head(0), count(0) {}
};

class ChartView;

//不支持动态切换通道数，也没必要
class CHART_EXPORT ChartManager : public QObject {
    Q_OBJECT
public:
    explicit ChartManager(int channelCount = 5, QObject *parent = nullptr); //默认支持5个通道
    ~ChartManager();
    void start();                                           // 启动图表管理器
    void stop();                                            // 停止图表管理器
    void setPeriod(int ms);                                 // 设置图表更新周期(单位ms),默认100ms
    // 图表显示相关操作接口
    void setChannelVisible(int channel, bool targetVisible, bool actualVisible);
    void setLegendName(int channel, const QString& name);
    void setWindowTime(int time_s);                         // 设置自动模式窗口的长度：单位s
    void setMode(int mode);                                 // 0:自动模式， 1:手动模式
    void setAbsTime(bool isAbs);
    void setBackColor(int color);                           // 设置图表背景色
    void clearShow();                                       // 一键清空显示
    void stopShow();                                        // 一键停止显示
    int  getMode() const;                                   // 获取当前模式
    QChart* getChart() const;                               // 获取图表对象
    void setChartView(ChartView* chartView);                // 绑定视图对象

    // 数据操作相关接口
    void addData(int channel, float target, float actual);  // 添加一个数据
    void addData(int channel, float target, float actual, qint64 timestamp);
    void addData(const QList<ChannelData>& dataNum);        // 添加一批数据
    void importData(const QString& fileName);               // 导入数据, 会覆盖原始数据，请先保存
    void exportData(const QString& fileName, qint64 startTime, qint64 endTime);
    void updateData();                                      // 用户可以手动更新数据
    void updateAll();                                       // 显示所有数据，采用LTTB降采样

signals:
    void exportDataFinished(bool success, const QString& message);
    void importDataFinished(bool success, const QString& message);

private:
    class Private;
    Private     *pimpl;
};

#endif // CHART_MANAGER_H__