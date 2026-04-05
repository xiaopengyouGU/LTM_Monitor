#ifndef LOG_TABLE_MODEL_H
#define LOG_TABLE_MODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QVariantMap>
#include <QDateTime>
#include <atomic>
#include "record_manager.h"   // 包含 RecordData 定义

//自定义模型，用于模型视图结构（显示时间、优先级两列，时间在前）
class LogTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
    ColTimestamp = 0,           //时间列
    ColPriority,                //优先级列
    ColContent,                 //内容列
    ColCount
};

    explicit LogTableModel(QObject *parent = nullptr);

    // 移动语义：直接接管数据，无拷贝
    void setLogs(QList<RecordData>&& logs);
    // 如果需要拷贝版本（一般不用）
    void setLogs(const QList<RecordData>& logs);

    const QList<RecordData>& logs() const { return m_logs; }

    // QAbstractTableModel 接口
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // 为代理模型提供原始数据角色
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<RecordData> m_logs;
};

#endif // LOG_TABLE_MODEL_H