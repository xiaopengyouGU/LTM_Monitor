#include "record_manager.h"
#include "record_manager_private.h"
#include "record_worker.h"
#include "log_worker.h"
#include <QApplication>
#include <QDir>

static QString getDatabaseFilePath(const QString& type) {
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dbDir(appDir);
    dbDir.cdUp();
    dbDir.cdUp();
    
    // 创建 DBs 子目录（如果不存在）
    if (!dbDir.exists("DBs")) {
        dbDir.mkdir("DBs");
    }
    // 无论目录是否已存在，都要进入 DBs 目录
    dbDir.cd("DBs");
    
    QString date = QDateTime::currentDateTime().toString("yyyy_MM");
    return dbDir.filePath(QString("%1_%2.db3").arg(type).arg(date));
}
// ========== RecordManager::Private 实现 ==========
RecordManager::Private::Private(RecordManager *parent)
    : QObject(parent), m_manager(parent)
{
    record_thread = new QThread(this);
    m_worker = new RecordWorker();
    m_log = new LogWorker(this);
    m_worker->moveToThread(record_thread);
    m_worker->connectLog(m_log);                    //绑定日志工作对象

    // 连接内部信号到 RecordWorker
    connect(this, &Private::initDataBase, m_worker, &RecordWorker::do_initDataBase);
    connect(this, &Private::logToDataBase, m_worker, &RecordWorker::do_logToDataBase);
    connect(this, &Private::dataInsert, m_worker, &RecordWorker::insertData);
    connect(this, &Private::dataDelete, m_worker, &RecordWorker::deleteData);
    connect(this, &Private::dataQuery, m_worker, &RecordWorker::queryData);
    connect(this, &Private::parseLogFileSig, m_worker, &RecordWorker::do_parseLogFile);
    connect(this, &Private::parseDatabaseSig, m_worker, &RecordWorker::do_parseDatabase);
    connect(m_log, &LogWorker::logToDataBase, m_worker, &RecordWorker::do_logToDataBase);
    connect(record_thread, &QThread::started, m_worker, &RecordWorker::start);

    // 连接 RecordWorker 的信号到本类的 do_ 槽函数
    connect(m_worker, &RecordWorker::parseFinished, this, &Private::do_parseFinished);
    connect(m_worker, &RecordWorker::parseError, this, &Private::do_parseError);
    connect(m_worker, &RecordWorker::parseProgress, this, &Private::do_parseProgress);
    connect(m_worker, &RecordWorker::dataBaseOpened, this, &Private::do_dataBaseOpened);
    connect(m_worker, &RecordWorker::dataBaseClose, this, &Private::do_dataBaseClose);
}

RecordManager::Private::~Private()
{
    stop();
    delete m_worker;
}

void RecordManager::Private::start()
{
    record_thread->start();
    if (logDB_name.isEmpty())
        logDB_name = getDatabaseFilePath("log");
    if (dataDB_name.isEmpty())
        dataDB_name = getDatabaseFilePath("data");
    emit initDataBase(logDB_name, dataDB_name);             //初始化数据库
}

void RecordManager::Private::stop()
{
    //避免出现定时器被UI线程关闭的情况
    QMetaObject::invokeMethod(m_worker, &RecordWorker::stop, Qt::BlockingQueuedConnection);
    record_thread->quit();
    record_thread->wait();
}

void RecordManager::Private::setLogDB(const QString& fileName) { logDB_name = fileName; }
void RecordManager::Private::setDataDB(const QString& fileName) { dataDB_name = fileName; }

void RecordManager::Private::logDebug(const QString& msg)   { m_log->debug(msg); }
void RecordManager::Private::logInfo(const QString& msg)    { m_log->info(msg); }
void RecordManager::Private::logWarn(const QString& msg)    { m_log->warn(msg); }
void RecordManager::Private::logError(const QString& msg)   { m_log->error(msg); }

void RecordManager::Private::createData(RecordData& data)
{
    QVariantMap processData;
    processData["cycle_id"] = 1;
    processData["ultrasonic_pos"] = 12.34;
    processData["ultrasonic_vel"] = 5.67;
    processData["ultrasonic_force"] = 98.2;
    processData["ultrasonic_power"] = 150.0;
    processData["plunger_pos"] = 45.6;
    processData["plunger_vel"] = 2.3;
    processData["plunger_pressure"] = 12.8;
    processData["plunger_rpm"] = 3000;
    processData["clamp_pos"] = 0.0;
    processData["clamp_vel"] = 0.5;
    processData["clamp_force"] = 100.2;
    data.extra = processData;
    data.priority = 0;
    data.content = "工艺数据";
}

void RecordManager::Private::insertData(const RecordData& data) { emit dataInsert(data); }
void RecordManager::Private::deleteData(const RecordData& data) { emit dataDelete(data); }
void RecordManager::Private::queryData(const RecordData& data) { emit dataQuery(data); }
void RecordManager::Private::parseLogFile(const QString& filePath) { emit parseLogFileSig(filePath); }
void RecordManager::Private::parseDatabase(const QString& dbPath) { emit parseDatabaseSig(dbPath); }

// 槽函数：转发 RecordWorker 信号到 RecordManager
void RecordManager::Private::do_parseFinished(QList<RecordData>* logs)
{
    emit m_manager->parseFinished(logs);
}
void RecordManager::Private::do_parseError(const QString& error)
{
    emit m_manager->parseError(error);
}
void RecordManager::Private::do_parseProgress(int current, int total)
{
    emit m_manager->parseProgress(current, total);
}
void RecordManager::Private::do_dataBaseOpened(bool success, const QString& msg)
{
    emit m_manager->dataBaseOpened(success, msg);
}
void RecordManager::Private::do_dataBaseClose()
{
    emit m_manager->dataBaseClose("");
}

// ========== RecordManager 公共接口实现 ==========
RecordManager::RecordManager(QObject *parent)
    : QObject(parent), pimpl(new Private(this)){}

RecordManager::~RecordManager()
{
    delete pimpl;
}

void RecordManager::setLogDB(const QString& fileName) { pimpl->setLogDB(fileName); }
void RecordManager::setDataDB(const QString& fileName) { pimpl->setDataDB(fileName); }
void RecordManager::start() { pimpl->start(); }
void RecordManager::stop() { pimpl->stop(); }
void RecordManager::logDebug(const QString& msg)   { pimpl->logDebug(msg); }
void RecordManager::logInfo(const QString& msg)    { pimpl->logInfo(msg); }
void RecordManager::logWarn(const QString& msg)    { pimpl->logWarn(msg); }
void RecordManager::logError(const QString& msg)   { pimpl->logError(msg); }
void RecordManager::createData(RecordData& data) { pimpl->createData(data); }
void RecordManager::insertData(const RecordData& data) { pimpl->insertData(data); }
void RecordManager::deleteData(const RecordData& data) { pimpl->deleteData(data); }
void RecordManager::queryData(const RecordData& data) { pimpl->queryData(data); }
void RecordManager::parseLogFile(const QString& filePath) { pimpl->parseLogFile(filePath); }
void RecordManager::parseDatabase(const QString& dbPath) { pimpl->parseDatabase(dbPath); }