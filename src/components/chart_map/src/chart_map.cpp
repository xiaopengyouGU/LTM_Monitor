#include "chart_map.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

ChartMap::ChartMap() = default;
ChartMap::~ChartMap() = default;

// 槽位键：channel(1~10)，0 保留为"未映射"
quint8 ChartMap::slotKey(quint8 channel)
{
    return channel;
}

// 源键合成：sourceType(8) + sourceKey(40) + signalIndex(16)，一次 QHash 命中
quint64 ChartMap::sourceKeyOf(quint8 sourceType, quint64 sourceKey, quint16 signalIndex)
{
    return ((quint64)sourceType << 56) | ((sourceKey & 0xFFFFFFFFFFULL) << 16) | signalIndex;
}

bool ChartMap::addEntry(const ChartMapEntry &entry, QString *error)
{
    if (entry.channel < 1 || entry.channel > ChannelCount) {
        if (error) *error = QString("通道号越界：%1（有效 1~%2）").arg(entry.channel).arg(ChannelCount);
        return false;
    }
    const quint8 sk = slotKey(entry.channel);
    if (m_slots.contains(sk)) {
        if (error) *error = QString("槽位冲突：通道%1已被映射").arg(entry.channel);
        return false;
    }
    const quint64 src = sourceKeyOf(entry.sourceType, entry.sourceKey, entry.signalIndex);
    if (m_sourceIndex.contains(src)) {
        if (error) *error = QString("同一信号重复映射：%1").arg(entry.signalName);
        return false;
    }
    m_slots.insert(sk, entry);
    m_sourceIndex.insert(src, sk);
    return true;
}

// 整表替换（编辑器用）：先校验全部，再原子交换，任一条失败则整表不变
bool ChartMap::setEntries(const QList<ChartMapEntry> &entries, QString *error)
{
    QHash<quint8, ChartMapEntry> newSlots;
    QHash<quint64, quint8> index;
    for (const ChartMapEntry &e : entries) {
        if (e.channel < 1 || e.channel > ChannelCount) {
            if (error) *error = QString("通道号越界：%1（有效 1~%2）").arg(e.channel).arg(ChannelCount);
            return false;
        }
        const quint8 sk = slotKey(e.channel);
        if (newSlots.contains(sk)) {
            if (error) *error = QString("槽位冲突：通道%1").arg(e.channel);
            return false;
        }
        const quint64 src = sourceKeyOf(e.sourceType, e.sourceKey, e.signalIndex);
        if (index.contains(src)) {
            if (error) *error = QString("同一信号重复映射：%1").arg(e.signalName);
            return false;
        }
        newSlots.insert(sk, e);
        index.insert(src, sk);
    }
    m_slots = newSlots;         // 校验通过后原子替换
    m_sourceIndex = index;
    return true;
}

bool ChartMap::removeEntry(quint8 channel)
{
    const quint8 sk = slotKey(channel);
    auto it = m_slots.constFind(sk);
    if (it == m_slots.constEnd())
        return false;
    m_sourceIndex.remove(sourceKeyOf(it->sourceType, it->sourceKey, it->signalIndex));
    m_slots.remove(sk);
    return true;
}

ChartMapEntry ChartMap::entry(quint8 channel) const
{
    return m_slots.value(slotKey(channel));
}

QList<ChartMapEntry> ChartMap::entries() const
{
    QList<ChartMapEntry> list;
    QList<quint8> keys = m_slots.keys();
    std::sort(keys.begin(), keys.end());        // 按槽位键排序，编辑器显示稳定
    for (quint8 k : keys)
        list.append(m_slots.value(k));
    return list;
}

void ChartMap::clear()
{
    m_slots.clear();
    m_sourceIndex.clear();
}

ChartMapEntry ChartMap::findBySource(quint8 sourceType, quint64 sourceKey, quint16 signalIndex) const
{
    return m_slots.value(m_sourceIndex.value(sourceKeyOf(sourceType, sourceKey, signalIndex)));
}

bool ChartMap::loadJson(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("无法打开文件：%1").arg(file.errorString());
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        if (error) *error = QString("JSON 解析失败：%1").arg(perr.errorString());
        return false;
    }

    clear();
    int idx = 0;
    for (const QJsonValue &v : doc.array()) {
        idx++;
        if (!v.isObject())
            continue;
        const QJsonObject obj = v.toObject();
        ChartMapEntry e;
        e.sourceType  = (quint8)obj.value("sourceType").toInt(ChartMap_CANFD);
        e.sourceKey   = obj.value("sourceKey").toString().toULongLong();
        e.signalIndex = (quint16)obj.value("signalIndex").toInt();
        e.signalName  = obj.value("signalName").toString();
        e.channel     = (quint8)obj.value("channel").toInt();
        QString err;
        if (!addEntry(e, &err)) {
            if (error) *error = QString("第%1项无效：%2").arg(idx).arg(err);
            return false;
        }
    }
    return true;
}

bool ChartMap::saveJson(const QString &filePath, QString *error) const
{
    QJsonArray arr;
    const QList<ChartMapEntry> list = entries();
    for (const ChartMapEntry &e : list) {
        QJsonObject o;
        o.insert("sourceType", e.sourceType);
        o.insert("sourceKey", QString::number(e.sourceKey));   // 字符串，避免 quint64 精度损失
        o.insert("signalIndex", e.signalIndex);
        o.insert("signalName", e.signalName);
        o.insert("channel", e.channel);
        arr.append(o);
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QString("无法写入文件：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}
