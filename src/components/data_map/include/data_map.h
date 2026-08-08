#ifndef DATA_MAP_H
#define DATA_MAP_H

#include <QByteArray>
#include <QHash>
#include "data_map_def.h"

// DataMap：CAN 数据映射组件（纯逻辑，无 UI、无线程）。
// 职责：加载协议定义（DBC / 用户自定义 JSON），按位解码 CAN/CAN-FD 帧为信号值。
// 通道映射与动态显示由上层（中转站）负责，本组件不做任何 UI 耦合。
class DATA_MAP_EXPORT DataMap
{
public:
    DataMap();
    ~DataMap();

    bool loadDbc(const QString &filePath, QString *error = nullptr);      // 加载 DBC 协议文件
    bool loadJson(const QString &filePath, QString *error = nullptr);     // 加载用户自定义协议文件（JSON）
    bool addMessage(const DataMapMessage &msg, QString *error = nullptr); // 编程方式添加消息定义
    // 批量添加消息定义（解析器内部使用）
    bool addMessages(const QList<DataMapMessage> &msgs, QString *error = nullptr);
    // 解码一帧（按位提取，自动处理大小端 / 有无符号 / MUX / 值表），isExt：判断是否拓展帧
    bool decode(uint32_t id, bool isExt, const QByteArray &payload,
                QList<DataMapDecodedSignal> &result, QString *error = nullptr) const;

    bool contains(uint32_t id, bool isExt) const;   // 判断当前 ID 是否在映射表中 
    QList<DataMapMessage> messages() const;
    void clear();                                   // 清空 ID 映射表

private:
    QHash<quint64, DataMapMessage> m_messages;      // key：isExt << 32 | id
};

#endif // DATA_MAP_H
