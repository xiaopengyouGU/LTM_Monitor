#include "log_analysis.h"
#include "ui_log_analysis.h"
#include "record_manager.h"   // 完整定义，用于调用解析接口
#include "log_table_model.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QCoreApplication>
#include <QHeaderView>
#include <QComboBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QSet>
#include <QSortFilterProxyModel>
#include <utility>

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

// ============================================================
// 私有实现（Pimpl）：UI 与模型/代理全部收敛于此
// ============================================================
class LogAnalysis::Private
{
public:
    explicit Private(LogAnalysis *log) : log(log) {}

    void setup();                       // UI 创建 + 模型/代理 + 排序过滤接线
    void connectManager(RecordManager *manager);
    void onOpenFile();                  // 打开 .log 文件
    void onOpenDB();                    // 打开 .db3 数据库
    void onParseFinished(QList<RecordData> *logs);
    void onParseError(const QString &error);
    void onParseProgress(int current, int total);
    void onSortFieldChanged(int index);
    void onSortOrderChanged();
    void onFilterChanged();
    void loadLogs(QList<RecordData> &&logs);    // 移动数据到模型
    void updateStatus();                        // 更新故障状态显示

    LogAnalysis              *log = nullptr;
    Ui::LogAnalysis          *ui = nullptr;
    LogTableModel            *m_model = nullptr;    // 数据模型
    QSortFilterProxyModel    *m_proxy = nullptr;    // 排序/过滤代理
    RecordManager            *m_manager = nullptr;  // 记录管理器（外部传入）
};

void LogAnalysis::Private::setup()
{
    ui = new Ui::LogAnalysis;
    ui->setupUi(log);
    log->setWindowFlags(Qt::Window);    // 设置为独立窗口（带标题栏和边框）

    // 创建模型和代理
    m_model = new LogTableModel(log);
    m_proxy = new PriorityFilterProxy(log);         // 内存管理交给 Qt 负责
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole);             // 按原始值排序（时间戳、优先级整数）
    ui->tableView->setModel(m_proxy);               
    // 设置时间列最小宽度，确保完整显示
    ui->tableView->setColumnWidth(LogTableModel::ColTimestamp, 160);
    ui->tableView->setColumnWidth(LogTableModel::ColPriority, 60);
    // 内容列自动拉伸
    ui->tableView->horizontalHeader()->setSectionResizeMode(LogTableModel::ColContent, QHeaderView::Stretch);

    // 默认只显示 Info 级别（可根据需求调整）
    QSet<int> enabled;
    enabled.insert(1);  // Info
    static_cast<PriorityFilterProxy *>(m_proxy)->setEnabledPriorities(enabled);

    // 连接排序控件（lambda 以 log 为接收上下文，随控件销毁自动断开）
    connect(ui->comboField, QOverload<int>::of(&QComboBox::currentIndexChanged), log,
            [this](int index) { onSortFieldChanged(index); });
    connect(ui->radioUp, &QRadioButton::toggled, log, [this](bool) { onSortOrderChanged(); });
    connect(ui->radioDown, &QRadioButton::toggled, log, [this](bool) { onSortOrderChanged(); });

    // 连接过滤复选框
    connect(ui->chkDebug, &QCheckBox::toggled, log, [this](bool) { onFilterChanged(); });
    connect(ui->chkInfo,  &QCheckBox::toggled, log, [this](bool) { onFilterChanged(); });
    connect(ui->chkWarn,  &QCheckBox::toggled, log, [this](bool) { onFilterChanged(); });
    connect(ui->chkError, &QCheckBox::toggled, log, [this](bool) { onFilterChanged(); });

    // 初始排序：时间升序
    onSortFieldChanged(0);
    ui->radioUp->setChecked(true);
}

void LogAnalysis::Private::connectManager(RecordManager *manager)
{
    if (m_manager == manager)       return;
    // 断开旧连接
    if (m_manager) {
        disconnect(m_manager, nullptr, log, nullptr);
    }
    m_manager = manager;
    if (m_manager) 
    {   // 绑定解析相关信号
        connect(m_manager, &RecordManager::parseFinished, log,
                [this](QList<RecordData> *logs) { onParseFinished(logs); });
        connect(m_manager, &RecordManager::parseError, log,
                [this](const QString &error) { onParseError(error); });
        connect(m_manager, &RecordManager::parseProgress, log,
                [this](int current, int total) { onParseProgress(current, total); });
        // 注意：不在此处启动 manager，由外部调用 manager->start()
    }
}

void LogAnalysis::Private::onOpenFile()
{
    if (!m_manager) {
        QMessageBox::warning(log, "错误", "未绑定记录管理器");
        return;
    }
        
    QString appDir = QCoreApplication::applicationDirPath();
    QDir logDir(appDir);
    logDir.cdUp();
    logDir.cdUp();
    QString logsPath = logDir.filePath("logs");
    QString fileName = QFileDialog::getOpenFileName(log, "打开日志文件", logsPath,
                                                    "日志文件 (*.log);;所有文件 (*)");
    if (fileName.isEmpty())
        return;

    ui->btnOpenFile->setEnabled(false);
    ui->btnOpenDB->setEnabled(false);
    ui->labInfo->setText("状态：正在解析文件...");

    m_manager->parseLogFile(fileName);
}

void LogAnalysis::Private::onOpenDB()
{
    if (!m_manager) {
        QMessageBox::warning(log, "错误", "未绑定记录管理器");
        return;
    }

    // 获取 DBs 目录路径（复用 getDatabaseFilePath 中的逻辑）
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dbDir(appDir);
    dbDir.cdUp();
    dbDir.cdUp();
    QString dbsPath = dbDir.filePath("DBs");
    QString dbPath = QFileDialog::getOpenFileName(log, "打开数据库", dbsPath,
                                                  "SQLite数据库 (*.db3);;所有文件 (*)");
    if (dbPath.isEmpty())
        return;

    ui->btnOpenFile->setEnabled(false);
    ui->btnOpenDB->setEnabled(false);
    ui->labInfo->setText("状态：正在读取数据库...");

    m_manager->parseDatabase(dbPath);           // 开始异步解析数据库数据
}

void LogAnalysis::Private::onParseFinished(QList<RecordData> *logs)
{
    if (!logs) {
        onParseError("解析结果为空");
        return;
    }
    loadLogs(std::move(*logs));
    delete logs;   // 释放指针容器（数据已被模型接管）
    updateStatus();                 //更新故障状态显示

    ui->btnOpenFile->setEnabled(true);
    ui->btnOpenDB->setEnabled(true);
    ui->labInfo->setText(ui->labInfo->text().contains("未") ? "状态：未发现故障日志" : "状态：发现故障日志");
}

void LogAnalysis::Private::onParseError(const QString &error)
{
    QMessageBox::warning(log, "错误", error);
    ui->btnOpenFile->setEnabled(true);
    ui->btnOpenDB->setEnabled(true);
    ui->labInfo->setText("状态：解析失败");
}

void LogAnalysis::Private::onParseProgress(int current, int total)
{
    if (total > 0)
        ui->labInfo->setText(QString("状态：已读取 %1 / %2 条").arg(current).arg(total));
    else
        ui->labInfo->setText(QString("状态：已处理 %1 行").arg(current));
}

void LogAnalysis::Private::loadLogs(QList<RecordData> &&logs)
{
    m_model->setLogs(std::move(logs));      // 模型直接接管数据，保证安全
    int total = m_model->rowCount();
    ui->labCount->setText(QString("记录条数：%1").arg(total));
    onFilterChanged();                      // 刷新过滤后数量
}

void LogAnalysis::Private::updateStatus()
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

void LogAnalysis::Private::onSortFieldChanged(int index)
{   // 索引0对应时间列，1对应优先级列（与 UI 下拉框顺序一致）
    int column = (index == 0) ? LogTableModel::ColTimestamp : LogTableModel::ColPriority;
    Qt::SortOrder order = ui->radioUp->isChecked() ? Qt::AscendingOrder : Qt::DescendingOrder;
    m_proxy->sort(column, order);
}

void LogAnalysis::Private::onSortOrderChanged()
{
    onSortFieldChanged(ui->comboField->currentIndex());
}

void LogAnalysis::Private::onFilterChanged()
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

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
LogAnalysis::LogAnalysis(QWidget *parent)
    : QWidget(parent)
    , pimpl(new Private(this))
{
    pimpl->setup();
}

LogAnalysis::~LogAnalysis()
{
    delete pimpl;
}

void LogAnalysis::connectManager(RecordManager *manager)
{
    pimpl->connectManager(manager);
}

void LogAnalysis::on_btnOpenFile_clicked() { pimpl->onOpenFile(); }
void LogAnalysis::on_btnOpenDB_clicked()   { pimpl->onOpenDB(); }
