#include "log_table_model.h"
#include "record_manager.h"     // RecordData 完整定义

// ============================================================
// 私有实现（Pimpl）：数据存储收敛于此
// ============================================================
class LogTableModel::Private
{
public:
    QList<RecordData> m_logs;
};

LogTableModel::LogTableModel(QObject *parent)
    : QAbstractTableModel(parent)
    , pimpl(new Private)
{
}

LogTableModel::~LogTableModel()
{
    delete pimpl;
}

void LogTableModel::setLogs(QList<RecordData> &&logs)
{
    beginResetModel();
    pimpl->m_logs = std::move(logs);    // 转移资源，原 logs 变为空
    endResetModel();
}

void LogTableModel::setLogs(const QList<RecordData> &logs)
{
    beginResetModel();
    pimpl->m_logs = logs;               // 拷贝（不常用，保留兼容）
    endResetModel();
}

const QList<RecordData> &LogTableModel::logs() const
{
    return pimpl->m_logs;
}

int LogTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return pimpl->m_logs.size();
}

int LogTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return ColCount;                    // 共3列：时间、优先级、内容
}

QVariant LogTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= pimpl->m_logs.size())
        return QVariant();

    const RecordData &log = pimpl->m_logs.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTimestamp: {
            QDateTime dt = QDateTime::fromMSecsSinceEpoch(log.timestamp);
            return dt.toString("yyyy-MM-dd HH:mm:ss.zzz");
        }
        case ColPriority: {
            switch (log.priority) {
            case 0: return "Debug";
            case 1: return "Info";
            case 2: return "Warning";
            case 3: return "Error";
            default: return QString("Level%1").arg(log.priority);
            }
        }
        case ColContent: {
            return log.content;         //返回内容
        }
        default: return QVariant();
        }
    }
    else if (role == Qt::UserRole) {
        // 用于排序和过滤的原始数据
        switch (index.column()) {
        case ColTimestamp: return log.timestamp;
        case ColPriority:  return log.priority;
        default:           return QVariant();
        }
    }
    else if (role == Qt::ToolTipRole) {
        return QVariant();
    }

    return QVariant();
}

QVariant LogTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case ColTimestamp: return "时间";
    case ColPriority:  return "优先级";
    case ColContent:   return "日志信息";
    default:           return QVariant();
    }
}

QHash<int, QByteArray> LogTableModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractTableModel::roleNames();
    roles[Qt::UserRole] = "rawData";
    return roles;
}
