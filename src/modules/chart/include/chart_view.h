#ifndef __CHART_VIEW_H__
#define __CHART_VIEW_H__

#include <QObject>
#include <QChartView>
#include <QValueAxis>
#include <QWheelEvent>
#include <QMouseEvent>

#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif

class ChartManager;                 //前向声明

class CHART_EXPORT ChartView : public QChartView
{
    Q_OBJECT
public:
    ChartView(QWidget *parent = nullptr);
    void setChartManager(ChartManager *manager);
    void zoomAxis(QValueAxis *axis, double center, qreal factor);
protected: 
    void wheelEvent(QWheelEvent *event) override;                   //滚轮缩放图表               
    void mousePressEvent(QMouseEvent *event) override;              //左键拖拽平移曲线
    void mouseMoveEvent(QMouseEvent *event) override;              //鼠标移动，平移更新
    void mouseReleaseEvent(QMouseEvent *event) override;            //鼠标释放，停止平移
private:
    // 为了方便，可以设置一个 ChartManager 指针用于获取模式
    ChartManager *m_manager = nullptr;
    bool isPanning;
    QPointF lastPanPos;
};

#endif