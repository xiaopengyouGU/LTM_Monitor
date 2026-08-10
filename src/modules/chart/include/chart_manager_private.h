#ifndef CHART_MANAGER_PRIVATE_H__
#define CHART_MANAGER_PRIVATE_H__

#include "chart_manager.h"
#include "chart_def.h"
#include <QStringList>
#include <QFutureWatcher>
#include <QTimer>
#include <QColor>
#include <QHash>
#include <QPair>

#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif

class ChartController;
class DataStorage;
class DataImporter;
class DataExporter;
class QThread;

// 并行取数结果：直接持有内部缓冲指针（零拷贝）
struct ParallelResult {
    ChannelData *data;       // 波形降采样缓冲（channel 在 ChannelData 内部）
    ChannelData *spectrum;   // 频谱结果缓冲（该通道无频谱视图时为 nullptr）
};

// 私有实现：所有真实逻辑都在这里，公共接口只做薄委托（参考 V0.2 设计）
class CHART_EXPORT ChartManager::Private : public QObject
{
    Q_OBJECT
public:
    Private(int channelCount, ChartManager *parent);
    ~Private();

    // ====== 生命周期 ======
    void start();
    void stop();
    void setPeriod(int ms);

    // ====== 数据写入（委托模型） ======
    void addData(int channel, double value);
    void addData(int channel, double time, double value);
    void addData(const QList<ChannelData>& dataList);

    // ====== 视图管理 ======
    int  createView(ViewType type);
    void removeView(int viewIndex);                           // viewIndex=-1，移除所有视图
    void attachChannel(int viewIndex, int channel);
    void detachChannel(int viewIndex, int channel);
    void setViewChannels(int viewIndex, const QList<int>& channels);
    QList<int> getViewChannels(int viewIndex) const;
    QWidget* getViewWidget(int viewIndex) const;

    // ====== 通道控制（全局生效） ======
    void setChannelName(int channel, const QString& name);
    void setChannelColor(int channel, const QColor& color);
    void setChannelVisible(int channel, bool visible);

    // ====== 坐标轴 ======
    void setViewRange(int viewIndex, double startTime, double endTime);
    void setAbsTime(int viewIndex, bool enabled);
    void setBackColor(int viewIndex, int color);

    // ====== FFT / 导入导出 ======
    ChannelData computeFFT(int channel, int nfft);
    void computeFFTInto(ChannelData &out, int channel, int nfft);   // 取最近窗口并算频谱，写 out（并行任务用）
    void exportData(const QString& fileName, double durationSeconds);
    void importData(const QString& fileName);

    // ====== 其他 ======
    void clearShow();
    void stopShow();

signals:
    void dataImport(const QString& fileName);
    void dataExport(const QString& fileName, double startTime, double endTime, const QStringList& nameList);

private slots:
    void do_importFinished(bool success, const QString& message);
    void do_importNames(const QStringList& names);
    void do_exportFinished(bool success, const QString& message);

private:
    void updateData();                                        // 每轮：窗口 -> 去重取数 -> 异步启动
    void broadcastResults(const QList<ParallelResult>& results);   // 主线程收尾广播

private:
    ChartManager *m_manager = nullptr;

    // 模型与调度
    int           m_channelCount = 10;
    DataStorage  *m_storage = nullptr;
    QTimer       *m_timer   = nullptr;
    int           m_period  = TIMER_PERIOD;
    int           m_threshold = POINT_THRESHOLD;
    int           m_nfft      = 2048;             // 频谱 FFT 点数
    double        m_windowLen = WINDOW_TIME;      // 自动跟随窗口（秒）
    double        m_latest = -1.0;                // 已知最新时间（由取数结果回填）
    qint64        m_lastVersion = -1;             // 上次刷新时的数据版本，无新数据时跳过空转
    bool          m_forceRefresh = false;         // 视图配置变化（拖拽/换通道等）强制刷新一次
    bool          isUpdating = false;
    bool          isImporting = false;            // 导入期间禁止刷新（含直接 updateData 调用）
    QFutureWatcher<ParallelResult> *m_activeWatcher = nullptr;   // 进行中的异步任务（析构时等待）
    QList<QFutureWatcher<ParallelResult>*> m_retiredWatchers;   // 被丢弃的批次（导入时），析构统一等待
    QThread      *m_dataThread = nullptr;         // 导入导出工作线程
    DataImporter *m_importer   = nullptr;         // 移入线程，无 parent
    DataExporter *m_exporter   = nullptr;

    // 视图集合（平行表）
    QList<ChartController*>      controllers;     // 每个视图一个 Controller
    QList<QList<int>>            viewChannels;    // 每个视图绑定的通道列表
    QList<ViewType>              viewTypes;       // 每个视图的类型（判断频谱视图）
    QList<QPair<double,double>>  viewRanges;      // 每个视图的显示范围（秒）
    QList<bool>                  viewAutoFollow;  // true=自动跟随最新，拖拽/设范围后置 false
    QList<bool>                  viewAbsTime;     // true=绝对时间
    QList<int>                   viewBackColor;   // 0=白，1=黑
    QList<ChannelData>           m_fftBufs;       // 每通道频谱结果缓冲（并行任务写，主线程零拷贝广播）
    QStringList                  m_channelNames;  // 通道名（导出列名，长度=通道数）
};

#endif // CHART_MANAGER_PRIVATE_H__
