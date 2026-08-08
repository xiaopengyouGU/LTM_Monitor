#include "data_map.h"
#include "data_map_parser.h"

// 消息查找键：isExt 放高 32 位，ID 放低 32 位
static quint64 messageKey(uint32_t id, bool isExt);  
// 按位提取原始值（纯位级引擎，同时支持 Intel / Motorola）
static bool extractRaw(const QByteArray &payload, const DataMapSignal &sig, qint64 &raw, QString &error);
// 解码一条消息中的所有有效信号（含 MUX 布局选择）
static bool decodeMessage(const DataMapMessage &msg, const QByteArray &payload,
                          QList<DataMapDecodedSignal> &out, QString &error);

DataMap::DataMap()  = default;
DataMap::~DataMap() = default;

bool DataMap::addMessage(const DataMapMessage &msg, QString *error)
{
    for (const DataMapSignal &s : msg.signalList)
    {
        if (s.chartChannel < 0 || s.chartChannel > 5)
        {
            if (error)
                *error = QString("信号 %1 的图表通道越界：%2（允许 0~5）").arg(s.name).arg(s.chartChannel);
            return false;
        }
    }
    quint64 key = messageKey(msg.id, msg.isExt);
    if (m_messages.contains(key)) {
        if (error)
            *error = QString("重复的消息定义：ID 0x%1（%2）").arg(msg.id, 0, 16).arg(msg.name);
        return false;
    }
    m_messages.insert(key, msg);
    return true;
}

bool DataMap::addMessages(const QList<DataMapMessage> &msgs, QString *error)
{
    for (const DataMapMessage &m : msgs) {
        for (const DataMapSignal &s : m.signalList)
        {
            if (s.chartChannel < 0 || s.chartChannel > 5)
            {
                if (error)
                    *error = QString("信号 %1 的图表通道越界：%2（允许 0~5）").arg(s.name).arg(s.chartChannel);
                return false;
            }
        }
        if (m_messages.contains(messageKey(m.id, m.isExt))) {
            if (error)
                *error = QString("重复的消息定义：ID 0x%1（%2）").arg(m.id, 0, 16).arg(m.name);
            return false;
        }
    }
    for (const DataMapMessage &m : msgs)
        m_messages.insert(messageKey(m.id, m.isExt), m);

    return true;
}

bool DataMap::loadDbc(const QString &filePath, QString *error)
{
    QList<DataMapMessage> msgs;
    QString err;
    if (!parseDbcFile(filePath, msgs, err)) {
        if (error)
            *error = err;
        return false;
    }
    return addMessages(msgs, error);
}

bool DataMap::loadJson(const QString &filePath, QString *error)
{
    QList<DataMapMessage> msgs;
    QString err;
    if (!parseJsonFile(filePath, msgs, err)) {
        if (error)
            *error = err;
        return false;
    }
    return addMessages(msgs, error);
}

bool DataMap::decode(uint32_t id, bool isExt, const QByteArray &payload,
                     QList<DataMapDecodedSignal> &result, QString *error) const
{
    result.clear();
    auto it = m_messages.constFind(messageKey(id, isExt));
    if (it == m_messages.constEnd()) {
        if (error)
            *error = QString("未定义的消息：ID 0x%1").arg(id, 0, 16);
        return false;
    }

    const DataMapMessage &msg = it.value();
    QString err;
    if (!decodeMessage(msg, payload, result, err)) {
        if (error)
            *error = err;
        return false;
    }
    return true;
}

bool DataMap::contains(uint32_t id, bool isExt) const
{
    return m_messages.contains(messageKey(id, isExt));
}

QList<DataMapMessage> DataMap::messages() const
{
    return m_messages.values();
}

void DataMap::clear()
{
    m_messages.clear();
}


/********************************** 内部函数 ************************************/
// 消息查找键：isExt 放高 32 位，ID 放低 32 位
static quint64 messageKey(uint32_t id, bool isExt)
{
    return ((quint64)(isExt ? 1 : 0) << 32) | id;
}

// 按位提取原始值（纯位级引擎，同时支持 Intel / Motorola）
static bool extractRaw(const QByteArray &payload, const DataMapSignal &sig, qint64 &raw, QString &error)
{
    const int len = sig.length;
    if (len < 1 || len > 64) {
        error = QString("信号 %1 位宽非法：%2").arg(sig.name).arg(len);
        return false;
    }

    // 计算信号覆盖的最大字节数
    int needBytes = 0;
    if (sig.byteOrder == DataMap_LittleEndian) {
        needBytes = ((sig.startBit + len - 1) >> 3) + 1;  // 位运算，性能更优
    } else {
        // Motorola：起始位为 MSB，本字节取完后跳到下一字节的 MSB
        int firstBits = (sig.startBit & 7) + 1;
        int rest = len > firstBits ? (len - firstBits) : 0;
        needBytes = (sig.startBit >> 3) + 1 + ((rest + 7) >> 3);
    }
    if (payload.size() < needBytes)
    {
        error = QString("信号 %1 超出数据长度（需要 %2 字节，实际 %3）").arg(sig.name).arg(needBytes).arg(payload.size());
        return false;
    }

    quint64 bits = 0;
    if (sig.byteOrder == DataMap_LittleEndian) {
        // Intel：起始位为 LSB，向高位延伸
        for (int k = 0; k < len; k++) {
            int bit = sig.startBit + k;
            bits |= (quint64)(((quint8)payload[bit >> 3] >> (bit & 7)) & 1) << k;
        }
    } else {
        // Motorola：起始位为 MSB（锯齿形编号），先取本字节低位方向，再到下一字节 MSB
        int byteIdx = sig.startBit >> 3;
        int bitPos = sig.startBit & 7;   // 字节内位位置，7 = MSB
        for (int k = 0; k < len; k++) {
            bits = (bits << 1) | (((quint8)payload[byteIdx] >> bitPos) & 1);
            if (bitPos == 0) {
                bitPos = 7;     // 本字节最低位取完，跳到下一字节的 MSB
                byteIdx++;
            } else {
                bitPos--;
            }
        }
    }

    // 符号扩展（two's complement）
    if (sig.valueType == DataMap_Signed && len < 64) {
        if (bits & (1ULL << (len - 1)))
            bits |= (~0ULL << len);
    }
    raw = (qint64)bits;
    return true;
}

// 解码一条消息中的所有有效信号（含 MUX 布局选择）
static bool decodeMessage(const DataMapMessage &msg, const QByteArray &payload,
                          QList<DataMapDecodedSignal> &out, QString &error)
{
    // MUX 子模块：先提取选择信号的值
    const DataMapSignal *selector = nullptr;
    for (const DataMapSignal &s : msg.signalList) {
        if (s.muxType == DataMap_MuxSelector) {
            selector = &s;
            break;
        }
    }
    qint64 selectorRaw = 0;
    if (selector) {
        if (!extractRaw(payload, *selector, selectorRaw, error))
            return false;
    }

    for (const DataMapSignal &s : msg.signalList) {
        // 非当前布局的多路信号，跳过
        if (s.muxType == DataMap_Muxed && s.muxValue != (int)selectorRaw)
            continue;

        qint64 raw = 0;
        if (!extractRaw(payload, s, raw, error))
            return false;

        DataMapDecodedSignal d;
        d.name = s.name;
        d.unit = s.unit;
        d.raw = raw;
        d.value = (double)raw * s.factor + s.offset;
        d.chartChannel = s.chartChannel;
        d.isTarget = s.isTarget;
        if (!s.valueTable.isEmpty()) {
            auto it = s.valueTable.constFind((int)raw);
            if (it != s.valueTable.constEnd())
                d.text = it.value();
        }
        out.append(d);
    }
    return true;
}
