#ifndef LOG_ANALYSIS_H
#define LOG_ANALYSIS_H

#include <QWidget>

class RecordManager;    // 前向声明，提高编译速度

// 日志分析器：打开 .log/.db3 文件，表格展示 + 排序/过滤。
// 内部状态（模型/代理/记录管理器）全部收敛在 Private（Pimpl）中。
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

private:
    Q_DISABLE_COPY(LogAnalysis)
    class Private;
    Private *pimpl = nullptr;
};

#endif // LOG_ANALYSIS_H
