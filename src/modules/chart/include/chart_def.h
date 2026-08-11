#ifndef CHART_DEF_H__
#define CHART_DEF_H__
//图表相关定义头文件
#include <QColor>

// 这个头文件要谨慎改动，请及时更新注释

// 图表相关参数
const int    POINT_THRESHOLD = 2400;    // 绘图时的曲线点数阈值（实时显示）
const int    LTTB_THRESHOLD  = 4500;    // LTTB降采样模式最大点数（用于数据分析），LTTB_THRESHOLD 必须大于 POINT_THRESHOLD
const double WINDOW_TIME     = 30.0;    // 自动窗口默认时间（单位：s）
const int    TIMER_PERIOD    = 100;     // 默认图表更新周期：100ms

// 数据存储相关参数，采用环形缓冲区存储（数据不能太多，否则读取性能跟不上）
// 最大通道数不能再提高了（60），因为性能跟不上
const int   DEFUALT_CHANNEL_NUMS   = 10;        // 默认通道数
const int   MAX_CHANNEL_NUMS       = 60;        // 最大通道数
const int   MAX_CHANNEL_POINTS     = 240000;    // 最大通道点数：对应 40min，100Hz采样
const int   MIN_CHANNEL_POINTS     = 60000;     // 最小通道点数：对应 10min，100Hz采样

#endif