#ifndef LOG_TABLE_MODEL_H
#define LOG_TABLE_MODEL_H

#include <QAbstractTableModel>
#include <QHash>
#include <QList>

struct RecordData;      // 前向声明（完整定义在 record_manager.h，仅 cpp 需要）

// 自定义模型，用于模型视图结构（显示时间、优先级、内容三列）。
// 数据存储收敛在 Private（Pimpl）中；模型通知（begin/endResetModel）由外层负责。
class LogTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColTimestamp = 0,           // 时间列
        ColPriority,                // 优先级列
        ColContent,                 // 内容列
        ColCount
    };

    explicit LogTableModel(QObject *parent = nullptr);
    ~LogTableModel();

    // 移动语义：直接接管数据，无拷贝
    void setLogs(QList<RecordData> &&logs);
    // 如果需要拷贝版本（一般不用）
    void setLogs(const QList<RecordData> &logs);

    const QList<RecordData> &logs() const;

    // QAbstractTableModel 接口
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // 为代理模型提供原始数据角色
    QHash<int, QByteArray> roleNames() const override;

private:
    Q_DISABLE_COPY(LogTableModel)
    class Private;
    Private *pimpl = nullptr;
};

#endif // LOG_TABLE_MODEL_H
