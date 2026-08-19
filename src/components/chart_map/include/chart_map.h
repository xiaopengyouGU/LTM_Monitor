#ifndef CHART_MAP_H
#define CHART_MAP_H

#include <QHash>
#include <QList>
#include "chart_map_def.h"

// ChartMap：数据源信号 → 图表槽位的统一映射表（纯逻辑，无 UI、无线程）。
// 职责：维护映射表（增删改查）、10 槽唯一性校验、JSON 读写、运行时源键查找。
// 数据源解码出的信号用 (sourceType, sourceKey, signalIndex) 查表，命中即映射到图表通道；
// 运行热路径走 QHash O(1) 查找，编辑器/配置走 entries()，两者解耦。
class CHART_MAP_EXPORT ChartMap
{
public:
    ChartMap();
    ~ChartMap();

    // 槽位规模：10 = 10 通道（图表实际支持 32 通道，映射表取 10 保留余量）
    static constexpr int ChannelCount = 10;
    static constexpr int SlotCount    = 10;

    // 增删改查
    bool addEntry(const ChartMapEntry &entry, QString *error = nullptr);  // 唯一性校验：channel 不冲突
    bool setEntries(const QList<ChartMapEntry> &entries, QString *error = nullptr);  // 整表替换（编辑器用，先校验后原子交换）
    bool removeEntry(quint8 channel);                      // 按槽位删除
    ChartMapEntry entry(quint8 channel) const;             // 按槽位查询（未配置返回空项）
    QList<ChartMapEntry> entries() const;                                 // 全表（编辑器用，按槽位排序）
    void clear();                                                         // 清空

    // 运行时查找：数据源信号 → 映射项（未映射返回 channel=0 的空项）
    ChartMapEntry findBySource(quint8 sourceType, quint64 sourceKey, quint16 signalIndex = 0) const;

    // 持久化：整表 JSON 读写（数组，源键用字符串存避免 quint64 精度损失）
    bool loadJson(const QString &filePath, QString *error = nullptr);
    bool saveJson(const QString &filePath, QString *error = nullptr) const;

private:
    static quint8   slotKey(quint8 channel);              // 槽位键：channel(1~5)
    static quint64  sourceKeyOf(quint8 sourceType, quint64 sourceKey, quint16 signalIndex);

    QHash<quint64, quint8> m_sourceIndex;   // 源键 → 槽位键（运行时 O(1) 查找）
    QHash<quint8, ChartMapEntry> m_slots;   // 槽位键 → 映射项（表的真身）
};

#endif // CHART_MAP_H
