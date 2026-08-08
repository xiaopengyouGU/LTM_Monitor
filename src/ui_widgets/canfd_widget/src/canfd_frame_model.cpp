#include "canfd_frame_model.h"

#include <QDateTime>

// 列 -> CanfdFrameRow 字段查表（Col_Index 单独处理）
const QString CanfdFrameRow::*const CanfdFrameModel::s_fields[CanfdFrameModel::Col_Count] = {
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

CanfdFrameModel::CanfdFrameModel(int maxRows, QObject *parent)
    : QAbstractTableModel(parent), m_capacity(maxRows)
{
    m_buf.resize(m_capacity);   // 预分配，写入 O(1)，无动态扩容/头部搬移
}

int CanfdFrameModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_count;
}

int CanfdFrameModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : Col_Count;
}

QVariant CanfdFrameModel::data(const QModelIndex &index, int role) const
{
    // index isValid 时，row() >= 0 和 column() >= 0 
    if (!index.isValid() || index.row() >= m_count)
        return QVariant();
    // m_head 在 [0, capacity)，row 在 [0, count]，所以 m_head + row < 2*capacity
    // 因此，此处的环形缓冲区下标不需要 取模操作，改成减法性能更优
    int idx = m_head + index.row();
    if (idx >= m_capacity)
        idx -= m_capacity;    
    const Row &r = m_buf.at(idx);
    int column = index.column();
    if (role == Qt::TextAlignmentRole) {
        if (column != Col_Data)
            return int(Qt::AlignCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role != Qt::DisplayRole)
        return QVariant();

    // 查表：数据已由中转站解析为字符串，序号入表时预格式化，显示零分支零加工
    if (column >= Col_Count)
        return QVariant();
    if (column == Col_Index)
        return r.seqStr;
    return r.row.*s_fields[column];
}

QVariant CanfdFrameModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    switch (section) {
        case Col_Index:      return QString("序号");
        case Col_SysTime:    return QString("系统时间");
        case Col_DevTime:    return QString("时间标识");
        case Col_Channel:    return QString("通道");
        case Col_Direction:  return QString("收发");
        case Col_Id:         return QString("ID");
        case Col_FrameFormat:return QString("Frame");
        case Col_FrameType:  return QString("类型");
        case Col_Dlc:        return QString("DLC");
        case Col_CANFD:      return QString("CAN-FD");
        case Col_Data:       return QString("数据");
        default:             return QVariant();
    }
}

void CanfdFrameModel::appendRows(const QList<CanfdFrameRow> &rows)
{
    // 一次最多入 capacity 行（多余丢弃，模型只保留 capacity 行），
    // 同时保证 m_head + removed < 2*capacity，取模可改为条件减法
    int n = qMin(rows.size(), m_capacity);
    if (n <= 0)             return;

    // 容量不足：整批移除最旧数据（一次模型通知，无逐元素搬移）
    int removed = (m_count + n > m_capacity) ? (m_count + n - m_capacity) : 0;
    if (removed > 0) {
        beginRemoveRows(QModelIndex(), 0, removed - 1);
        m_head += removed;                       // m_head + removed < 2*capacity，减法即可
        if (m_head >= m_capacity)
            m_head -= m_capacity;
        m_count -= removed;
        endRemoveRows();
    }

    // 批量写入环形缓冲（O(n)，一次模型通知）
    int first = m_count;
    beginInsertRows(QModelIndex(), first, first + n - 1);
    for (int i = 0; i < n; i++) {
        int idx = m_head + m_count;              // m_head + m_count < 2*capacity，减法即可
        if (idx >= m_capacity)
            idx -= m_capacity;
        Row &slot = m_buf[idx];
        slot.row  = rows.at(i);
        slot.seqStr = QString::number(++m_seq);
        m_count++;
    }
    endInsertRows();
}

void CanfdFrameModel::clear()
{
    beginResetModel();
    m_head = 0;
    m_count = 0;
    m_seq = 0;
    endResetModel();
}
/********************************** 内部实现 *****************************************/
// 数据长度 -> DLC 编码（CAN-FD）
static int dlcOf(int len)
{
    if (len <= 8)  return len;
    // 利用 CAN-FD DLC 编码规律：超过 8 字节时，DLC = 8 + (len - 8 + 3) / 4
    // 9-64 字节映射到 DLC 9-15
    int dlc = 8 + ((len - 8 + 3) >> 2);         // 位运算，性能更好
    if (dlc > 15) dlc = 15;
    return dlc;
}

// QStringLiteral(str) 无运行时开销，高频操作中，有极强的性能优势！！！
// DLC 码 -> 显示字符串（静态表，零分配）
static const QString s_dlcStr[16] = {
    QStringLiteral("0"),  QStringLiteral("1"),  QStringLiteral("2"),  QStringLiteral("3"),
    QStringLiteral("4"),  QStringLiteral("5"),  QStringLiteral("6"),  QStringLiteral("7"),
    QStringLiteral("8"),  QStringLiteral("9"),  QStringLiteral("10"), QStringLiteral("11"),
    QStringLiteral("12"), QStringLiteral("13"), QStringLiteral("14"), QStringLiteral("15")
};

static const char kHexTable[] = "0123456789ABCDEF";

// 64 位数值 -> 大写 hex（无前导零）
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

// 数值 -> 固定宽度大写 hex（不足补 0，一次分配）
static QString toHexFixed(quint64 v, int digits)
{
    QString s(digits, QLatin1Char('0'));
    for (int i = digits - 1; i >= 0; i--) {
        s[i] = QLatin1Char(kHexTable[v & 0xF]);
        v >>= 4;
    }
    return s;
}

// 数值 -> 大写 hex（无前导零，保持原显示习惯）
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

// 数据转 hex 显示：x| 11 22 33（hex 表直拼，零逐字节分配）
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

// 帧 -> 显示行（格式化在源头做一次，模型直接存储）
CanfdFrameRow CanfdFrameRow::fromFrame(const CanfdFrame &frame, bool isTx)
{
    CanfdFrameRow r;
    // 热路径：逐帧执行，字面量用 QStringLiteral（静态数据，无堆分配）
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