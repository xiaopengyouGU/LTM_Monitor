#ifndef CHART_MANAGER_PRIVATE_H__
#define CHART_MANAGER_PRIVATE_H__

class QLineSeries;
class QValueAxis;
class DataStorage;
class DataImporter;
class DataExporter;

#include "chart_manager.h"
#include <QFutureWatcher>
#include <QChart>

#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif


class ChartView;

//并行任务结果结构体
struct SeriesResult {
    int channel;
    QList<QPointF>* points;
    bool isTarget;
};

//并行任务
struct SeriesTask {
    int channel;
    bool isTarget;
    qint64 viewStart;
    qint64 viewEnd;
    bool isAbs;
    int threshold;
    bool useLTTB;   // 是否使用 LTTB（用于 updateAll）
};

class CHART_EXPORT ChartManager::Private : public QObject
{
    Q_OBJECT
public:
    Private(int channelCount, ChartManager *parent);
    ~Private();
    void start();
    void stop();
    void setPeriod(int ms);
    void setChannelVisible(int ch, bool targetVisible, bool actualVisible);
    void setLegendName(int ch, const QString& target, const QString& actual);  // 复用通道，减少内存开销
    void setMode(int mode);
    void setAbsTime(bool isAbs);
    void setBackColor(int color);
    void setWindowTime(int windowTime);                     // 设置自动模式窗口的长度：单位s
    void setChartView(ChartView* chartView);                // 绑定视图对象
    void clearShow();
    void stopShow();
    int  getMode() const;
    void addData(int ch, float target, float actual);
    void addData(int channel, float target, float actual, qint64 timestamp);
    void addData(const QList<ChannelData>& dataNum);         // 添加一批数据
    void importData(const QString& fileName);
    void exportData(const QString& fileName, qint64 startTime, qint64 endTime);
    void updateData();
    void updateAll();

signals:
    void dataImport(const QString& fileName);
    void dataExport(const QString& fileName, qint64 startTime, qint64 endTime);

private slots:
    void do_XRangeChanged(qreal min, qreal max);
    void do_exportFinished(bool success, const QString& message);
    void do_importFinished(bool success, const QString& message);
private:
    void buildChart();
    void buildData();
    void buildTimer();
    void _adjustYAxis(int num);                                 //Y轴自动缩放
    void _setBackColor();
    void _getViewRange(qint64& viewStart, qint64& viewEnd);     //直接获取当前视图范围
private:
    void updateTimerPeriod();               //自动更新窗口定时器
    void finishUpdateAll();
    void processTasksParallel(const QList<SeriesTask>& tasks, bool isUpdateAll = false);
    void onAllResultsReady(const QList<SeriesResult>& results);
private:
    ChartManager *m_manager;

    QChart *m_chart;
    QList<QLineSeries*> m_targetSeries;
    QList<QLineSeries*> m_actualSeries;
    QValueAxis *m_xAxis;
    QValueAxis *m_yAxis;
    qreal m_timewindow;                     //自动模式窗口长度：单位s           
    int m_mode;                             //模式0:1
    int m_color;                            //背景色
    int m_period;                           //图表刷新周期：单位ms(支持 50-250)
    int m_count;                            //支持的通道数量最多10个，默认5个。
    bool m_useAbsTime;
    qint64 m_baseTime;                      //基准时间戳
    qint64 m_endTime;                       //最新时间戳
    qint64 m_lastNow;                       //上次更新的时间戳

    QThread *data_thread;
    DataStorage *m_storage;
    DataImporter *m_importer;
    DataExporter *m_exporter;
    QTimer *m_timer;
    ChartView     *m_chartView;             // 视图对象
    bool m_isUpdating = false;   // 是否正在执行并行降采样任务
    QFutureWatcher<SeriesResult>* m_activeWatcher = nullptr;  // 跟踪当前活动的并行任务
// private:                         // 压力测试
//     int m_updateTotal = 0;
//     int m_updateSkipped = 0;
//     int m_taskStarted = 0;
//     QTimer* m_statsTimer = nullptr;
};

#endif
