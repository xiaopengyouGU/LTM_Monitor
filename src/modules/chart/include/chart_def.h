#ifndef CHART_DEF_H__
#define CHART_DEF_H__
//图表相关定义头文件
#include <QColor>

// 这个头文件要谨慎改动，请及时更新注释

// 图表相关参数
const int    POINT_THRESHOLD = 2500;    // 绘图时的曲线点数阈值（实时显示）
const int    LTTB_THRESHOLD  = 4500;    // LTTB降采样模式最大点数（用于数据分析），LTTB_THRESHOLD 必须大于 POINT_THRESHOLD
const double WINDOW_TIME     = 30.0;    // 自动窗口默认时间（单位：s）
const int    TIMER_PERIOD    = 100;     // 默认图表更新周期：100ms

// 数据存储相关参数，采用环形缓冲区存储（数据不能太多，否则读取性能跟不上）
// 最大通道数不能再提高了（60），因为性能跟不上
const int   DEFUALT_CHANNEL_NUMS   = 10;        // 默认通道数
const int   MAX_CHANNEL_NUMS       = 60;        // 最大通道数
const int   MAX_CHANNEL_POINTS     = 360000;    // 最大通道点数：对应 1h，100Hz采样
const int   MIN_CHANNEL_POINTS     = 60000;     // 最小通道点数：对应 10min，100Hz采样

// 曲线颜色定义数组
static const QColor targetColors[10] = {
    Qt::red,                     // 红
    QColor(255, 128, 0),         // 橙
    Qt::magenta,                 // 品红
    Qt::blue,                    // 蓝
    Qt::darkBlue,                // 深蓝
    QColor(128, 0, 128),         // 紫
    QColor(255, 192, 203),       // 粉
    QColor(139, 69, 19),         // 棕
    QColor(128, 128, 0),         // 橄榄绿
    QColor(75, 0, 130)           // 靛蓝
};

static const QColor actualColors[10] = {
    Qt::green,                   // 绿
    Qt::yellow,                  // 黄
    Qt::darkGreen,               // 深绿
    Qt::cyan,                    // 青
    Qt::darkYellow,              // 深黄
    QColor(144, 238, 144),       // 浅绿
    QColor(255, 165, 0),         // 橙（与目标值橙色不同）
    QColor(138, 43, 226),        // 紫罗兰
    QColor(135, 206, 235),       // 天蓝
    QColor(255, 127, 80)         // 珊瑚
};


#endif