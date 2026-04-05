#include "log_analysis.h"
#include "ui_log_analysis.h"
#include "record_manager.h"   // 完整定义，用于调用解析接口
#include <QFileDialog>
#include <QMessageBox>

// 优先级过滤器代理（内部类）
class PriorityFilterProxy : public QSortFilterProxyModel
{
public:
    PriorityFilterProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}

    void setEnabledPriorities(const QSet<int> &priorities) {
        m_enabledPriorities = priorities;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override {
        if (m_enabledPriorities.isEmpty())
            return true;
        QModelIndex idx = sourceModel()->index(source_row, LogTableModel::ColPriority, source_parent);
        int priority = sourceModel()->data(idx, Qt::UserRole).toInt();
        return m_enabledPriorities.contains(priority);
    }

private:
    QSet<int> m_enabledPriorities;
};

LogAnalysis::LogAnalysis(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::LogAnalysis),
    m_manager(nullptr)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);  // 设置为独立窗口（带标题栏和边框）
    // 创建模型和代理
    m_model = new LogTableModel(this);
    m_proxy = new PriorityFilterProxy(this);        //内存管理交给Qt负责
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole);   // 按原始值排序（时间戳、优先级整数）
    ui->tableView->setModel(m_proxy);               
    // 设置时间列最小宽度，确保完整显示
    ui->tableView->setColumnWidth(LogTableModel::ColTimestamp, 160);
    ui->tableView->setColumnWidth(LogTableModel::ColPriority,60);
    // 内容列自动拉伸
    ui->tableView->horizontalHeader()->setSectionResizeMode(LogTableModel::ColContent, QHeaderView::Stretch);

    // 默认只显示 Info 级别（可根据需求调整）
    QSet<int> enabled;
    enabled.insert(1);  // Info
    static_cast<PriorityFilterProxy*>(m_proxy)->setEnabledPriorities(enabled);

    // 连接排序控件
    connect(ui->comboField, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LogAnalysis::do_sortFieldChanged);
    connect(ui->radioUp, &QRadioButton::toggled,  this, &LogAnalysis::do_sortOrderChanged);
    connect(ui->radioDown, &QRadioButton::toggled, this, &LogAnalysis::do_sortOrderChanged);

    // 连接过滤复选框
    connect(ui->chkDebug, &QCheckBox::toggled, this, &LogAnalysis::do_filterChanged);
    connect(ui->chkInfo,  &QCheckBox::toggled, this, &LogAnalysis::do_filterChanged);
    connect(ui->chkWarn,  &QCheckBox::toggled, this, &LogAnalysis::do_filterChanged);
    connect(ui->chkError, &QCheckBox::toggled, this, &LogAnalysis::do_filterChanged);

    // 初始排序：时间升序
    do_sortFieldChanged(0);
    ui->radioUp->setChecked(true);
}

LogAnalysis::~LogAnalysis()
{
    delete ui;
}

void LogAnalysis::connectManager(RecordManager *manager)
{
    if (m_manager == manager)       return;
    // 断开旧连接
    if (m_manager) {
        disconnect(m_manager, nullptr, this, nullptr);
    }
    m_manager = manager;
    if (m_manager) 
    {   // 绑定解析相关信号
        connect(m_manager, &RecordManager::parseFinished,  this, &LogAnalysis::do_parseFinished);
        connect(m_manager, &RecordManager::parseError,     this, &LogAnalysis::do_parseError);
        connect(m_manager, &RecordManager::parseProgress,  this, &LogAnalysis::do_parseProgress);
        // 注意：不在此处启动 manager，由外部调用 manager->start()
    }
}

void LogAnalysis::on_btnOpenFile_clicked()
{
    if (!m_manager) {
        QMessageBox::warning(this, "错误", "未绑定记录管理器");
        return;
    }
        
    QString appDir = QCoreApplication::applicationDirPath();
    QDir logDir(appDir);
    logDir.cdUp();
    logDir.cdUp();
    QString logsPath = logDir.filePath("logs");
    QString fileName = QFileDialog::getOpenFileName(this, "打开日志文件", logsPath,
                                                    "日志文件 (*.log);;所有文件 (*)");
    if (fileName.isEmpty())
        return;

    ui->btnOpenFile->setEnabled(false);
    ui->btnOpenDB->setEnabled(false);
    ui->labInfo->setText("状态：正在解析文件...");

    m_manager->parseLogFile(fileName);
}

void LogAnalysis::on_btnOpenDB_clicked()
{
    if (!m_manager) {
        QMessageBox::warning(this, "错误", "未绑定记录管理器");
        return;
    }

    // 获取 DBs 目录路径（复用 getDatabaseFilePath 中的逻辑）
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dbDir(appDir);
    dbDir.cdUp();
    dbDir.cdUp();
    QString dbsPath = dbDir.filePath("DBs");
    QString dbPath = QFileDialog::getOpenFileName(this, "打开数据库", dbsPath,
                                                  "SQLite数据库 (*.db3);;所有文件 (*)");
    if (dbPath.isEmpty())
        return;

    ui->btnOpenFile->setEnabled(false);
    ui->btnOpenDB->setEnabled(false);
    ui->labInfo->setText("状态：正在读取数据库...");

    m_manager->parseDatabase(dbPath);           //开始异步解析数据库数据
}

void LogAnalysis::do_parseFinished(QList<RecordData>* logs)
{
    if (!logs) {
        do_parseError("解析结果为空");
        return;
    }
    loadLogs(std::move(*logs));
    delete logs;   // 释放指针容器（数据已被模型接管）
    updateStatus();                 //更新故障状态显示

    ui->btnOpenFile->setEnabled(true);
    ui->btnOpenDB->setEnabled(true);
    ui->labInfo->setText(ui->labInfo->text().contains("未") ? "状态：未发现故障日志" : "状态：发现故障日志");
}

void LogAnalysis::do_parseError(const QString& error)
{
    QMessageBox::warning(this, "错误", error);
    ui->btnOpenFile->setEnabled(true);
    ui->btnOpenDB->setEnabled(true);
    ui->labInfo->setText("状态：解析失败");
}

void LogAnalysis::do_parseProgress(int current, int total)
{
    if (total > 0)
        ui->labInfo->setText(QString("状态：已读取 %1 / %2 条").arg(current).arg(total));
    else
        ui->labInfo->setText(QString("状态：已处理 %1 行").arg(current));
}

void LogAnalysis::loadLogs(QList<RecordData>&& logs)
{
    m_model->setLogs(std::move(logs));     //模型直接接管数据，保证安全
    int total = m_model->rowCount();
    ui->labCount->setText(QString("记录条数：%1").arg(total));
    do_filterChanged();   // 刷新过滤后数量
}

void LogAnalysis::updateStatus()
{
    bool hasError = false;
    const auto& logs = m_model->logs();
    for (const RecordData& log : logs) {
        if (log.priority == 3) {  // Error
            hasError = true;
            break;
        }
    }
    ui->labInfo->setText(hasError ? "状态：发现故障日志" : "状态：未发现故障日志");
}

void LogAnalysis::do_sortFieldChanged(int index)
{   // 索引0对应时间列，1对应优先级列（与 UI 下拉框顺序一致）
    int column = (index == 0) ? LogTableModel::ColTimestamp : LogTableModel::ColPriority;
    Qt::SortOrder order = ui->radioUp->isChecked() ? Qt::AscendingOrder : Qt::DescendingOrder;
    m_proxy->sort(column, order);
}

void LogAnalysis::do_sortOrderChanged()
{
    do_sortFieldChanged(ui->comboField->currentIndex());
}

void LogAnalysis::do_filterChanged()
{
    QSet<int> enabled;
    if (ui->chkDebug->isChecked()) enabled.insert(0);
    if (ui->chkInfo->isChecked())  enabled.insert(1);
    if (ui->chkWarn->isChecked())  enabled.insert(2);
    if (ui->chkError->isChecked()) enabled.insert(3);

    auto* filterProxy = static_cast<PriorityFilterProxy*>(m_proxy);
    filterProxy->setEnabledPriorities(enabled);

    int filteredCount = m_proxy->rowCount();
    ui->labCount->setText(QString("记录条数：%1 (过滤后 %2)").arg(m_model->rowCount()).arg(filteredCount));
}