#ifndef LOG_ANALYSIS_H
#define LOG_ANALYSIS_H

#include <QWidget>
#include <QSortFilterProxyModel>

// 前向声明，提高编译速度
class RecordManager;
class LogTableModel;
struct RecordData;   

namespace Ui {
class LogAnalysis;
}

class LogAnalysis : public QWidget
{
    Q_OBJECT

public:
    explicit LogAnalysis(QWidget *parent = nullptr);
    ~LogAnalysis();

    // 绑定记录管理器（外部传入，UI不负责创建）
    void connectManager(RecordManager *manager);

private slots:
    void on_btnOpenFile_clicked();   // 打开 .log 文件
    void on_btnOpenDB_clicked();     // 打开 .db3 数据库

    // 解析相关槽（do_前缀）
    void do_parseFinished(QList<RecordData>* logs);
    void do_parseError(const QString& error);
    void do_parseProgress(int current, int total);

    // 排序和过滤
    void do_sortFieldChanged(int index);
    void do_sortOrderChanged();
    void do_filterChanged();

private:
    void loadLogs(QList<RecordData>&& logs);   // 移动数据到模型
    void updateStatus();                       // 更新故障状态显示

    LogTableModel *m_model;                 // 数据模型
    QSortFilterProxyModel *m_proxy;         // 排序/过滤代理
    RecordManager *m_manager;               // 记录管理器（外部传入）

private:
    Ui::LogAnalysis *ui;
};

#endif // LOG_ANALYSIS_H