#ifndef CHART_CONTROLLER_H__
#define CHART_CONTROLLER_H__

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QWidget>
#include "chart_manager.h"      // ViewType（定义在 ChartManager 头中，保持不动）

#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif

// ChartController：渲染层适配器，封装 QCustomPlot 的所有操作。
// ChartManager 通过它控制显示，不直接接触 QCustomPlot API。
// 纯渲染层，无业务逻辑：不管数据从哪来、何时刷新、是否降采样。
class CHART_EXPORT ChartController : public QObject
{
    Q_OBJECT
public:
    explicit ChartController(QWidget *parent = nullptr);
    ~ChartController();

    // ====== 视图管理 ======
    QWidget* getWidget() const;                         // 获取 QCustomPlot 控件，供用户放置
    void setViewType(ViewType type);                    // 设置视图类型（切换即重建，通道清空）
    void clear();                                       // 清空所有曲线
    void clearData();                                   // 清空曲线数据，保留图形结构（clearShow 用）

    // ====== 数据更新 ======
    void updateData(int channel, const QList<double>& x, const QList<double>& y);
    void updateSpectrum(const QList<double>& freq, const QList<double>& mag);

    // ====== 通道管理 ======
    void addChannel(int channel, const QString& name = QString(), const QColor& color = Qt::red);
    void removeChannel(int channel);
    void setChannelVisible(int channel, bool visible);
    void setViewVisible(bool visible);                      // 隐藏/显示视图内所有曲线（含频谱/XY，stopShow 用）
    void setChannelName(int channel, const QString& name);
    void setChannelColor(int channel, const QColor& color);
    void setChannels(const QList<int>& channels);           // 批量设置；XY 视图按顺序取前两个为 A(X)/B(Y)
    void setXYChannels(const QList<int>& channels);         // XY：仅更新 A/B 映射，不重建曲线

    // ====== 坐标轴 ======
    void setXRange(double min, double max);
    void setYRange(double min, double max);
    void setXAxisTitle(const QString& title);
    void setYAxisTitle(const QString& title);

    // ====== 外观 ======
    void setBackColor(const QColor& color);

    // ====== 交互 ======
    void setInteraction(bool drag, bool zoom);
    void setRangeDragAxes(bool x, bool y);
    void setRangeZoomFactor(double xFactor, double yFactor);

    // ====== 刷新 ======
    void replot();                                      // 立即刷新
    void replotQueued();                                // 排队刷新（自动合并）

signals:
    void rangeChanged(double min, double max);          // X 轴范围变化（通知 ChartManager）
    void mouseMove(double x, double y);                 // 鼠标移动坐标（高频，调用方自行节流）

private:
    class Private;
    Private *pimpl;                                     // 私有实现，QCustomPlot 不泄漏到头文件
};

#endif // CHART_CONTROLLER_H__
