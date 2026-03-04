#ifndef __CHART_MANAGER_H__
#define __CHART_MANAGER_H__

#include <QObject>
#include <QChart>
#include <QLineSeries>
#include <QValueAxis>
#include <QTimer>
#include <limits>
#include "data_storage.h"


class ChartManager : public QObject {
    Q_OBJECT
public:
    ChartManager(QChart* chart);
    ~ChartManager();
    void setChannelVisible(int channel, bool targetVisible, bool actualVisible);
    void setLegendName(int channel, bool isTarget, const QString& name);
    void setMode(int mode);
    void setAbsTime(bool isAbs);
    void setBackColor(int color);                           //设置图表背景色
    int  getMode()  const { return m_mode; }                 //获取当前模式
    void updateData(DataStorage* storage);                  //100ms更新一次数据
    void updateAll(DataStorage* storage);                   //显示所有数据，采用LTTB降采样

private slots:
    void do_XRangeChanged(qreal min, qreal max);            //限制缩放显示，避免出现不该出现的值
private:
    void _adjustYAxis(int num = 800);                       //Y轴自动缩放，单通道取点数默认800，
    void _setBackColor();

private:
    QChart* m_chart;
    QList<QLineSeries*> m_targetSeries; //5条目标值曲线
    QList<QLineSeries*> m_actualSeries; //5条实际值曲线
    QValueAxis *m_xAxis;
    QValueAxis *m_yAxis;
    qreal m_timewindow;                 //显示时间窗口长度
    int m_mode;                         // 0： 自动模式， 1：手动模式
    int m_color;                        // 0： 白色，     1：黑色
    bool m_useAbsTime;                  //采用绝对时间轴
    qint64 m_baseTime;                  //基准时间戳
    qint64 m_endTime;                   //当前记录的最新时间戳，用于判断是否有新数据
    qint64 m_lastNow;                   //上次界面更新时的时间戳
};

#endif