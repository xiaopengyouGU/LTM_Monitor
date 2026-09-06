#ifndef CHART_MANAGER_H__
#define CHART_MANAGER_H__

#include <QObject>
#include <QString>
#include <QColor>
#include <QList>
// 为动态库添加导出宏！！！
// chart_manager.h
#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif

typedef enum {
    Color_White = 0,
    Color_Black,                    
}WindowColor;

typedef enum {
    Mode_Auto = 0,          // 自动模式
    Mode_Hand,              // 手动模式,1
}ShowMode;

typedef enum {              // 视图类型
    View_Waveform,          // 波形视图：显示通道的时域曲线，支持多通道叠加和多坐标轴
    View_Spectrum,          // 频谱视图：显示单通道的FFT频谱（单轴）
    View_WaveformSpectrum,  // 波形+频谱：上下分栏，上波形下频谱，通道同步
    View_XY                 // XY图：通道A为X轴，通道B为Y轴
}ViewType;

// 通道数据结构体
struct ChannelData {
    int channel;            // 通道号
    QList<double> times;    // 时间戳
    QList<double> values;   // 数值
};

// 默认曲线颜色：给通道取默认颜色（写自定义图表界面/对话框时直接取用，按通道号循环配色）
CHART_EXPORT QColor getChannelColor(int channel);

class CHART_EXPORT ChartManager : public QObject {
    Q_OBJECT
public:
    // ====== 生命周期 ======
    explicit ChartManager(int channelCount = 10, QObject *parent = nullptr);
    ~ChartManager();

    void start();           // 启动图表
    void stop();            // 停止图表
    void setPeriod(int ms); // 刷新周期（ms），默认100

    // ====== 显示模式 ======
    void setMode(ShowMode mode);            // Mode_Auto=自动跟随最新，Mode_Hand=手动保持当前范围
    ShowMode getMode() const;

    // ====== 数据写入，时间戳必须单调递增 ======
    void addData(int channel, double value);                    // 自动打时间戳（实时采集）
    void addData(int channel, double time, double value);       // 用户指定时间戳（导入/回放）
    void addData(const QList<ChannelData>& dataList);           // 批量导入：time < 0 时利用内部时间戳（仅适用于一帧多通道），time < 0 时，利用内部时间戳：仅适用于一帧多通道。

    // ====== 视图管理 ======
    int  createView(ViewType type);                             // 创建视图，返回视图索引
    void removeView(int viewIndex);                             // viewIndex=-1，移除所有视图                             // viewIndex=-1,移除所有视图
    void attachChannel(int viewIndex, int channel);             // 把通道显示到指定视图
    void detachChannel(int viewIndex, int channel);
    void setViewChannels(int viewIndex, const QList<int>& channels);
    QList<int> getViewChannels(int viewIndex) const;            // 返回视图对应的所有通道
    QWidget* getViewWidget(int viewIndex) const;                // 获取视图控件，供用户放置

    // ====== 通道控制 ======
    void setChannelName(int channel, const QString& name);      // 通道图例名，同时影响导出列名
    void setChannelColor(int channel, const QColor& color);     // 设置通道颜色
    void setChannelVisible(int channel, bool visible);          // 设置通道可见性

    // ====== 坐标轴 ======
    void setViewRange(int viewIndex, double startTime, double endTime); // 设置X轴范围
    void setWindowLen(double seconds);                  // 自动跟随显示窗口长度（秒）
    void setAbsTime(int viewIndex, bool enabled);               // true=绝对时间，false=相对时间
    void setBackColor(int viewIndex, int color);                // 0=白，1=黑

    // ====== FFT ======
    ChannelData computeFFT(int channel, int nfft = 1024);       // 返回频谱（频率, 线性幅值）

    // ====== 导入导出 ======
    void exportData(const QString& fileName, double durationSeconds = -1.0); // -1=全量导出
    void importData(const QString& fileName);

    // ====== 其他 ======
    void clearShow();   // 清空显示
    void stopShow();    // 停止显示：所有通道不可见（数据保留）

signals:
    void exportFinished(bool success, const QString& message);
    void importFinished(bool success, const QString& message);
private:
    class Private;
    Private *pimpl;
};


#endif // CHART_MANAGER_H__
