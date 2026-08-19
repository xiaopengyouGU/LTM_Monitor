#ifndef DATA_MAP_DEF_H
#define DATA_MAP_DEF_H

#include <QHash>
#include <QList>
#include <QString>
#include <cstdint>

#if defined(DATA_MAP_LIBRARY)
#  define DATA_MAP_EXPORT Q_DECL_EXPORT
#else
#  define DATA_MAP_EXPORT Q_DECL_IMPORT
#endif

// 字节序：与 DBC 一致，Intel 为小端（@1），Motorola 为大端（@0）
enum DataMapByteOrder
{
    DataMap_LittleEndian = 0,
    DataMap_BigEndian = 1
};

// 信号数值类型
enum DataMapValueType
{
    DataMap_Unsigned = 0,
    DataMap_Signed = 1
};

// 信号多路复用类型（MUX 子模块）
enum DataMapMuxType
{
    DataMap_MuxNone = 0,        // 公共信号：所有布局都有效
    DataMap_MuxSelector = 1,    // 多路选择信号（DBC 中的 M）
    DataMap_Muxed = 2           // 多路信号（DBC 中的 m<value>），仅在选择信号等于 muxValue 时有效
};

// 信号定义
struct DataMapSignal
{
    QString          name;
    int              startBit = 0;              // DBC 起始位编号
    int              length = 8;                // 信号位宽（1 ~ 64）
    DataMapByteOrder byteOrder = DataMap_LittleEndian;
    DataMapValueType valueType = DataMap_Unsigned;
    float            factor = 1.0;              // 缩放系数：物理值 = raw * factor + offset
    float            offset = 0.0;
    QString          unit;                      // 单位
    float            minVal = 0.0;              // 最小允许值
    float            maxVal = 0.0;              // 最大范围
    DataMapMuxType   muxType = DataMap_MuxNone;
    int              muxValue = 0;              // muxType == DataMap_Muxed 时有效
    int              chartChannel = 0;          // 图表通道：0 = 不映射，1~5 = CH1~CH5
    QHash<int, QString> valueTable;             // 枚举值表（DBC VAL_），可选
};

// 消息定义
struct DataMapMessage
{
    uint32_t id = 0;                            // 11 位或 29 位 ID
    bool     isExt = false;                     // true：扩展帧（29 位 ID）
    QString  name;
    int      len = 8;                           // 期望的数据长度（字节）
    QList<DataMapSignal> signalList;
};

// 解码结果
struct DataMapDecodedSignal
{
    QString name;                               // 信号名
    QString unit;                               // 单位
    float   value = 0.0;                        // 物理值：raw * factor + offset
    qint64  raw = 0;                            // 原始值（未缩放）
    QString text;                               // 枚举文本（配置了值表时有效），否则为空
    quint16 signalIndex = 0;                // 信号在报文内序号（供图表映射表定位）
    int     chartChannel = 0;                   // 图表通道（透传自信号定义）
};

#endif // DATA_MAP_DEF_H
