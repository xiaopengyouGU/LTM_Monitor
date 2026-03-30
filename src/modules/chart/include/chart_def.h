#ifndef CHART_DEF_H__
#define CHART_DEF_H__
//图表相关定义头文件
#include <QColor>

//这个头文件要谨慎改动，因为会触发模块全量编译

//非专业人士请勿操作！
//非专业人士请勿操作！
//非专业人士请勿操作！

//专业人士操作后，请及时更新注释，避免误解
//专业人士操作后，请及时更新注释，避免误解
//专业人士操作后，请及时更新注释，避免误解

//ChartView组件相关参数
const int    DISTANCE_THRESHOLD = 30;   // 鼠标和X、Y轴靠近的响应阈值（最大值）
const double PAN_SPEED_FACTOR = 0.3;    // 平移阻尼因子， 可根据需要调整（0.1~0.5）
const double ZOOM_SMALL_FACTOR  = 0.8;  //缩小因子
const double ZOOM_BIG_FACTOR    = 1.2;  //放大因子

//图表相关参数
const int    POINT_THRESHOLD = 1500;    //绘图时的曲线点数阈值（实时显示）
const int    LTTB_THRESHOLD = 3000;     //LTTB采样模式最大点数（用于数据分析）
const double WINDOW_TIME  = 30.0;       //自动窗口默认时间（单位：s）
const int    MAX_WINDOW_TIME  = 60*60;  //自动模式最长窗口时间（单位：s）
const int    MIN_WINDOW_TIME  = 10;     //自动模式最短窗口时间（单位：s）
const int    WINDOW_COLOR = 0;          //窗口颜色，0：白色， 1：黑色
const int    SHOW_MODE    = 0;          //显示模式，0：自动， 1：手动
const int    TIMER_PERIOD = 100;        //默认图表更新周期：100ms
const qint64 MAX_WINDOW_MS = 60 * 60 * 1000;    // 限制最大窗口为60分钟，避免数据量过大
//图表动态调整刷新时间的参数
//当前窗口显示范围（单位:s）
const int   WINDOW_TIME_I    = 5 * 60;     //5分钟
const int   WINDOW_TIME_II   = 15 * 60;    //15分钟
const int   WINDOW_TIME_III  = 30 * 60;    //30分钟
const int   WINDOW_TIME_VI   = 60 * 60;    //60分钟
//图表刷新周期相应自动调整为原有的倍数（一一对应）
//这样在长窗口下（如30分钟）自动降低刷新频率，显著减少 CPU 消耗，
const int   PERIOD_MUL_I     = 1;          //1倍
const int   PERIOD_MUL_II    = 2;          //2倍
const int   PERIOD_MUL_III   = 4;          //4倍
const int   PERIOD_MUL_VI    = 8;          //8倍

//数据存储相关参数，采用环形缓冲区存储（数据不能太多，否则读取性能跟不上）
//真正的通道数是下面的参数的 2 倍
//最大通道数不能再提高了（10），因为性能跟不上 
const int   DEFUALT_CHANNEL_NUMS   = 5;         //默认通道数（每个通道包含实际值和目标值，成对）
const int   MAX_CHANNEL_NUMS       = 10;        //最大通道数（每个通道包含实际值和目标值，成对）
//下面的通道点数实际是指目标值/实际值的存储的点数。
const int   DEFUALT_CHANNEL_POINTS = 720000;    //默认通道点数：对应1h,200Hz采样 
const int   MAX_CHANNEL_POINTS = 720000;        //最大通道点数：对应1h,200Hz采样
const int   MIN_CHANNEL_POINTS = 120000;        //最小通道点数：对应10min，200Hz采样

//曲线颜色定义数组
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