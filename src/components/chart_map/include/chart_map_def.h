#ifndef CHART_MAP_DEF_H
#define CHART_MAP_DEF_H

#include <QString>
#include <cstdint>

#if defined(CHART_MAP_LIBRARY)
#  define CHART_MAP_EXPORT Q_DECL_EXPORT
#else
#  define CHART_MAP_EXPORT Q_DECL_IMPORT
#endif

// 数据源类型：统一映射表的定位维度之一
enum ChartMapSource
{
    ChartMap_CANFD = 0,     // CAN-FD 报文信号
    ChartMap_Modbus,        // Modbus RTU 寄存器
    ChartMap_LTM            // LTM 协议通道
};

// 统一映射项：数据源信号 → 图表槽位
// 槽位共 10 个：channel(1~10)；channel=0 表示不映射（仅表格/日志）
struct ChartMapEntry
{
    quint8  sourceType = ChartMap_CANFD;  // 数据源类型（ChartMapSource）
    quint64 sourceKey = 0;                // 源键：CAN-FD=messageKey(id|isExt)，Modbus=从站<<16|寄存器，LTM=通道号
    quint16 signalIndex = 0;              // 信号序号：CAN-FD=报文内信号序号，Modbus=寄存器内值序号，LTM=0
    QString signalName;                   // 信号名（显示/日志）
    quint8  channel = 0;                  // 图表通道：1~5；0 = 不映射
};

#endif // CHART_MAP_DEF_H
