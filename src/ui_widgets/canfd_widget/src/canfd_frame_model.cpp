#include "canfd_frame_model.h"

#include <QDateTime>

// ============================================================
// 私有实现（Pimpl）：环形缓冲/字段查表/序号状态收敛于此
// ============================================================
class CanfdFrameModel::Private
{
public:
    explicit Private(int maxRows) : m_capacity(maxRows)
    {
        m_buf.resize(m_capacity);   // 预分配，写入 O(1)，无动态扩容/头部搬移
    }

    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    void removeOldest(int removed);
    void appendNew(const QList<CanfdFrameRow> &rows, int n);
    void clear();

    struct Row
    {
        CanfdFrameRow row;
        QString       seqStr;        // 序号（入表时预格式化，显示零加工）
    };

    static const QString CanfdFrameRow::*const s_fields[CanfdFrameModel::Col_Count];  // 列 -> 字段查表

    QList<Row>  m_buf;          // 环形缓冲（预分配固定容量）
    int         m_capacity = 10000;
    int         m_head = 0;     // 逻辑第 0 行对应的物理索引
    int         m_count = 0;    // 有效行数
    quint64     m_seq = 0;
};

// 列 -> CanfdFrameRow 字段查表（Col_Index 单独处理）
const QString CanfdFrameRow::*const CanfdFrameModel::Private::s_fields[CanfdFrameModel::Col_Count] = {
    nullptr,                              // Col_Index
    &CanfdFrameRow::sysTime,              // Col_SysTime
    &CanfdFrameRow::devTime,              // Col_DevTime
    &CanfdFrameRow::channel,              // Col_Channel
    &CanfdFrameRow::direction,            // Col_Direction
    &CanfdFrameRow::id,                   // Col_Id
    &CanfdFrameRow::frameFormat,          // Col_FrameFormat
    &CanfdFrameRow::frameType,            // Col_FrameType
    &CanfdFrameRow::canfdType,            // Col_CANFD
    &CanfdFrameRow::dlc,                  // Col_Dlc
    &CanfdFrameRow::data,                 // Col_Data
};

int CanfdFrameModel::Private::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_count;
}

int CanfdFrameModel::Private::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : CanfdFrameModel::Col_Count;
}

QVariant CanfdFrameModel::Private::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_count)
        return QVariant();
    // m_head 在 [0, capacity)，row 在 [0, count)，所以 m_head + row < 2*capacity
    // 因此，此处的环形缓冲区下标不需要取模操作，改成减法性能更优
    int idx = m_head + index.row();
    if (idx >= m_capacity)
        idx -= m_capacity;
    const Row &r = m_buf.at(idx);
    const int column = index.column();
    if (role == Qt::TextAlignmentRole) {
        if (column != CanfdFrameModel::Col_Data)
            return int(Qt::AlignCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role != Qt::DisplayRole)
        return QVariant();

    // 查表：数据已由中转站解析为字符串，序号入表时预格式化，显示零分支零加工
    if (column >= CanfdFrameModel::Col_Count)
        return QVariant();
    if (column == CanfdFrameModel::Col_Index)
        return r.seqStr;
    return r.row.*s_fields[column];
}

QVariant CanfdFrameModel::Private::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    switch (section) {
        case CanfdFrameModel::Col_Index:      return QString("序号");
        case CanfdFrameModel::Col_SysTime:    return QString("系统时间");
        case CanfdFrameModel::Col_DevTime:    return QString("时间标识");
        case CanfdFrameModel::Col_Channel:    return QString("通道");
        case CanfdFrameModel::Col_Direction:  return QString("收发");
        case CanfdFrameModel::Col_Id:         return QString("ID");
        case CanfdFrameModel::Col_FrameFormat:return QString("Frame");
        case CanfdFrameModel::Col_FrameType:  return QString("类型");
        case CanfdFrameModel::Col_Dlc:        return QString("DLC");
        case CanfdFrameModel::Col_CANFD:      return QString("CAN-FD");
        case CanfdFrameModel::Col_Data:       return QString("数据");
        default:                              return QVariant();
    }
}

void CanfdFrameModel::Private::removeOldest(int removed)
{
    // 整批移除最旧数据：head 前移 + count 减少，无逐元素搬移。
    // 保证 m_head + removed < 2*capacity，取模可改为条件减法
    m_head += removed;
    if (m_head >= m_capacity)
        m_head -= m_capacity;
    m_count -= removed;
}

void CanfdFrameModel::Private::appendNew(const QList<CanfdFrameRow> &rows, int n)
{
    for (int i = 0; i < n; i++) {
        int idx = m_head + m_count;              // m_head + m_count < 2*capacity，减法即可
        if (idx >= m_capacity)
            idx -= m_capacity;
        Row &slot = m_buf[idx];
        slot.row  = rows.at(i);
        slot.seqStr = QString::number(++m_seq);
        m_count++;
    }
}

void CanfdFrameModel::Private::clear()
{
    m_head = 0;
    m_count = 0;
    m_seq = 0;
}

// ============================================================
// 公共接口：override 委托给私有实现（模型通知在外层）
// ============================================================
CanfdFrameModel::CanfdFrameModel(int maxRows, QObject *parent)
    : QAbstractTableModel(parent)
    , pimpl(new Private(maxRows))
{
}

int CanfdFrameModel::rowCount(const QModelIndex &parent) const
{
    return pimpl->rowCount(parent);
}

int CanfdFrameModel::columnCount(const QModelIndex &parent) const
{
    return pimpl->columnCount(parent);
}

QVariant CanfdFrameModel::data(const QModelIndex &index, int role) const
{
    return pimpl->data(index, role);
}

QVariant CanfdFrameModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    return pimpl->headerData(section, orientation, role);
}

void CanfdFrameModel::appendRows(const QList<CanfdFrameRow> &rows)
{
    if (rows.isEmpty() || pimpl->m_capacity <= 0) return;
    const int n = qMin(rows.size(), pimpl->m_capacity);

    // 容量不足：先整批移除最旧（通知与数据操作严格配对，不嵌套）
    const int removed = qMax(0, pimpl->m_count + n - pimpl->m_capacity);
    if (removed > 0) {
        beginRemoveRows(QModelIndex(), 0, removed - 1);
        pimpl->removeOldest(removed);
        endRemoveRows();
    }

    const int first = pimpl->m_count;
    beginInsertRows(QModelIndex(), first, first + n - 1);
    pimpl->appendNew(rows, n);
    endInsertRows();
}

void CanfdFrameModel::clear()
{
    beginResetModel();
    pimpl->clear();
    endResetModel();
}

// ============================================================
// 内部实现（帧 -> 显示行）
// ============================================================
static int dlcOf(int len)
{
    if (len <= 8)  return len;
    int dlc = 8 + ((len - 8 + 3) >> 2);         // 位运算，性能更好
    if (dlc > 15) dlc = 15;
    return dlc;
}

static const QString s_dlcStr[16] = {
    QStringLiteral("0"),  QStringLiteral("1"),  QStringLiteral("2"),  QStringLiteral("3"),
    QStringLiteral("4"),  QStringLiteral("5"),  QStringLiteral("6"),  QStringLiteral("7"),
    QStringLiteral("8"),  QStringLiteral("9"),  QStringLiteral("10"), QStringLiteral("11"),
    QStringLiteral("12"), QStringLiteral("13"), QStringLiteral("14"), QStringLiteral("15")
};

static const char kHexTable[] = "0123456789ABCDEF";

static QString toHexTime(quint64 v)
{
    char buf[17];
    int n = 0;
    do {
        buf[n++] = kHexTable[v & 0xF];
        v >>= 4;
    } while (v);
    QString s(n, QLatin1Char('0'));
    for (int i = 0; i < n; i++)
        s[i] = QLatin1Char(buf[n - 1 - i]);
    return s;
}

static QString toHexFixed(quint64 v, int digits)
{
    QString s(digits, QLatin1Char('0'));
    for (int i = digits - 1; i >= 0; i--) {
        s[i] = QLatin1Char(kHexTable[v & 0xF]);
        v >>= 4;
    }
    return s;
}

static QString toHexId(quint32 v)
{
    char buf[9];
    int n = 0;
    do {
        buf[n++] = kHexTable[v & 0xF];
        v >>= 4;
    } while (v);
    QString s(n, QLatin1Char('0'));
    for (int i = 0; i < n; i++)
        s[i] = QLatin1Char(buf[n - 1 - i]);
    return s;
}

static QString dataToHex(const CanfdFrame &frame)
{
    QString hex;
    hex.reserve(frame.len * 3 + 4);
    const int len = frame.len;
    if (len >= 10)
        hex += QLatin1Char('0' + len / 10);
    hex += QLatin1Char('0' + len % 10);
    hex += QLatin1String("| ");
    for (int i = 0; i < len; i++) {
        if (i > 0)
            hex += QLatin1Char(' ');
        const quint8 b = (quint8)frame.data.at(i);
        hex += QLatin1Char(kHexTable[b >> 4]);
        hex += QLatin1Char(kHexTable[b & 0xF]);
    }
    return hex;
}

CanfdFrameRow CanfdFrameRow::fromFrame(const CanfdFrame &frame, bool isTx)
{
    CanfdFrameRow r;
    r.sysTime = (frame.timestampEpochMs <= 0)
                ? QStringLiteral("-")
                : QDateTime::fromMSecsSinceEpoch(frame.timestampEpochMs).toString(QStringLiteral("HH:mm:ss.zzz"));
    r.devTime = (frame.timestampUs == 0)
                ? QStringLiteral("-")
                : QStringLiteral("0x") + toHexTime(frame.timestampUs);
    r.channel = (frame.channel == 0) ? QStringLiteral("1") : QStringLiteral("2");
    r.direction = isTx ? QStringLiteral("Tx") : QStringLiteral("Rx");
    r.id = QStringLiteral("0x") + toHexId(frame.rawId());
    r.frameFormat = frame.isEff() ? QStringLiteral("扩展帧") : QStringLiteral("标准帧");
    r.frameType   = frame.isRtr() ? QStringLiteral("远程帧") : QStringLiteral("数据帧");
    if (!frame.isFd)
        r.canfdType = QStringLiteral("CAN");
    else if (frame.brs())
        r.canfdType = QStringLiteral("CAN-FD加速");
    else
        r.canfdType = QStringLiteral("CAN-FD");
    r.dlc = s_dlcStr[dlcOf(frame.len)];
    r.data = dataToHex(frame);

    return r;
}
