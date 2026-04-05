#include "record_manager.h"
#include "record_worker.h"
#include "log_worker.h"
#include <QtSql>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

#define LOG_DEBUG(msg)  {if(m_log)  m_log->debug(msg);}
#define LOG_INFO(msg)   {if(m_log)  m_log->info(msg);}
#define LOG_WARN(msg)   {if(m_log)  m_log->warn(msg);}
#define LOG_ERROR(msg)  {if(m_log)  m_log->error(msg);}

RecordWorker::RecordWorker(QObject *parent):QObject(parent)
{
    //定时器初始化（先 new，再设置属性）
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::CoarseTimer);
    m_timer->setInterval(10 * 1000);          // 10s 写入一批数据，
    m_timer->setSingleShot(false);
    m_timer->stop();
    //绑定回调函数
    connect(m_timer, &QTimer::timeout, this, &RecordWorker::do_timer_timeout);
    //预分配内存
    log_num.reserve(100);                     //100条日志
    data_num.reserve(800);                    //800条工艺数据
    m_log = nullptr;
}

RecordWorker::~RecordWorker()
{   //最后写入数据
    do_timer_timeout();
}

void RecordWorker::connectLog(LogWorker *log)
{
    if(!log)        return; //判空
    m_log = log;
}

void RecordWorker::start()                                            //启动定时器
{
    m_timer->start();
}

void RecordWorker::stop()                                             //停止定时器
{
    m_timer->stop();
}                              

void RecordWorker::insertData(const RecordData& data)
{

}

void RecordWorker::deleteData(const RecordData& data)
{

}

void RecordWorker::queryData(const RecordData& data)
{

}

void RecordWorker::do_logToDataBase(int level, const QString &message)   //将日志写入数据库
{
    log_num.append(RecordData(level, message));                          //添加数据
}   

void RecordWorker::do_initDataBase(const QString& logName, const QString& dataName)
{   //打开工艺和日志数据库
    openLogDB(logName);
    openDataDB(dataName);
} 

void RecordWorker::do_timer_timeout()
{   //同时写入工艺和日志数据库
    writeLogNum();
    writeDataNum();
}

void RecordWorker::writeDataNum()                                  //写入工艺数据库
{
    if (data_num.isEmpty()) return;                             //没有待写入数据
    if (!data_DB.isOpen()) {
        LOG_WARN("工艺数据库未打开，无法写入工艺数据");
        return;
    }

    const int BATCH_SIZE = 200;  // 每批最多 200 条 (800 个参数 < 999)
    int total = data_num.size();

    if (!data_DB.transaction()) {
        LOG_ERROR("工艺数据库事务启动失败: " + data_DB.lastError().text());
        return;
    }

    bool ok = true;
    for (int start = 0; start < total && ok; start += BATCH_SIZE) {
        int end = qMin(start + BATCH_SIZE, total);
        int batchCount = end - start;

        QString sql = "INSERT INTO process_data (id, priority, timestamp, content) VALUES ";
        QStringList placeholders;
        QVariantList bindValues;
        bindValues.reserve(batchCount * 4);

        for (int i = start; i < end; ++i) {
            const RecordData& data = data_num[i];
            placeholders.append("(?, ?, ?, ?)");
            bindValues << data.id << data.priority << data.timestamp << data.content;
        }
        sql += placeholders.join(", ");

        QSqlQuery query(data_DB);
        query.prepare(sql);
        for (const QVariant& v : bindValues) {
            query.addBindValue(v);
        }
        if (!query.exec()) {
            LOG_ERROR(QString("批量插入工艺数据失败: %1").arg(query.lastError().text()));
            data_DB.rollback();
            ok = false;
            break;
        }
    }

    if (ok && !data_DB.commit()) {
        LOG_ERROR("工艺数据库事务提交失败: " + data_DB.lastError().text());
        ok = false;
    }

    if (ok) {
        data_num.clear();
    }
}

void RecordWorker::writeLogNum()                                   //写入日志数据库
{
    if (log_num.isEmpty()) return;                              //没有需要写入的日志

    if (!log_DB.isOpen()) {
        LOG_WARN("日志数据库未打开，无法写入日志");
        return;
    }
    // 构建多值 INSERT 语句
    QString sql = "INSERT INTO logs (id, priority, timestamp, content) VALUES ";
    QStringList placeholders;
    QVariantList bindValues;
    bindValues.reserve(log_num.size() * 4);                     // 预分配

    for (const RecordData& data : log_num) {
        placeholders.append("(?, ?, ?, ?)");
        bindValues << data.id << data.priority << data.timestamp << data.content;
    }
    sql += placeholders.join(", ");

    QSqlQuery query(log_DB);
    query.prepare(sql);
    for (const QVariant& v : bindValues) {
        query.addBindValue(v);
    }
    // 事务保证原子性（虽然单条多值插入本身就是原子的，但显式事务可确保日志一致性）
    if(!log_DB.transaction()) {
        LOG_ERROR("日志数据库事务启动失败: " + log_DB.lastError().text());
        return;
    }
    if(!query.exec()) {
        LOG_ERROR(QString("批量插入日志失败: %1").arg(query.lastError().text()));
        log_DB.rollback();
        return;
    }
    if(!log_DB.commit()) {
        LOG_ERROR("日志数据库事务提交失败: " + log_DB.lastError().text());
        return;
    }
    // 插入成功，清空日志数组
    log_num.clear();
}

void RecordWorker::openLogDB(const QString& logName)
{   // 日志数据库
    QString logDbPath = logName;
    log_DB = QSqlDatabase::addDatabase("QSQLITE", "log_connection");
    log_DB.setDatabaseName(logDbPath);
    if (!log_DB.open()) {
        LOG_ERROR("日志数据库打开失败: " + log_DB.lastError().text());
        return;
    }

    // 创建表（如果不存在）
    QSqlQuery query(log_DB);
    if (!query.exec("CREATE TABLE IF NOT EXISTS logs ("
                    "id INTEGER PRIMARY KEY, "
                    "priority INTEGER, "
                    "timestamp INTEGER, "
                    "content TEXT)")) {
        LOG_ERROR("创建日志表失败: " + query.lastError().text());
        return;
    }
}

void RecordWorker::openDataDB(const QString& dataName)
{   // 工艺数据库
    QString dataDbPath = dataName;
    data_DB = QSqlDatabase::addDatabase("QSQLITE", "data_connection");
    data_DB.setDatabaseName(dataDbPath);
    if (!data_DB.open()) {
        LOG_ERROR("工艺数据库打开失败: " + data_DB.lastError().text());
        return;
    }

    QSqlQuery query(data_DB);
    if (!query.exec("CREATE TABLE IF NOT EXISTS process_data ("
                    "id INTEGER PRIMARY KEY, "
                    "priority INTEGER, "
                    "timestamp INTEGER, "
                    "content TEXT)")) {
        LOG_ERROR("创建工艺表失败: " + query.lastError().text());
        return;
    } 
}

// ========== 解析功能实现 ==========

QList<RecordData>* RecordWorker::parseLogFileContent(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit parseError("无法打开文件：" + file.errorString());
        return nullptr;
    }

    // 估算有效记录条数：文件大小 / 平均每行字节数（约150~200），避免频繁 realloc
    qint64 fileSize = file.size();
    int estimatedLines = static_cast<int>(fileSize / 180) + 1000; // 保守估计，多预留1000
    QList<RecordData>* logs = new QList<RecordData>();
    logs->reserve(estimatedLines);              //预分配内存，避免数组频繁扩容。

    QTextStream stream(&file);
    int lineNum = 0;
    QRegularExpression regex(R"(\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]\s+\[(\w+)\]\s+(.*))");

    while (!stream.atEnd()) {
        QString line = stream.readLine();
        lineNum++;
        auto match = regex.match(line);
        if (!match.hasMatch())
            continue;

        QString timeStr = match.captured(1);
        QString levelStr = match.captured(2).toLower();
        QString content = match.captured(3);

        QDateTime dt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss.zzz");
        if (!dt.isValid())
            continue;

        qint64 timestamp = dt.toMSecsSinceEpoch();
        int priority = -1;
        if (levelStr == "debug") priority = 0;
        else if (levelStr == "info") priority = 1;
        else if (levelStr == "warning" || levelStr == "warn") priority = 2;
        else if (levelStr == "error") priority = 3;
        else continue;

        qint64 id = timestamp * 1000 + lineNum;
        logs->append(RecordData(id, priority, timestamp, content));

        if (lineNum % 1000 == 0)
            emit parseProgress(lineNum, 0);
    }
    LOG_INFO(QString("解析日志文件完成，共解析 %1 行，有效记录 %2 条").arg(lineNum).arg(logs->size()));
    return logs;
}

QList<RecordData>* RecordWorker::parseDatabaseContent(const QString& dbPath)
{
    QList<RecordData>* logs = new QList<RecordData>();

    // 将数据库操作放入独立作用域，确保 db 在 removeDatabase 前销毁
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "temp_parse_connection");
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            emit parseError("无法打开数据库：" + db.lastError().text());
            QSqlDatabase::removeDatabase("temp_parse_connection");
            delete logs;
            return nullptr;
        }

        // 获取总记录数
        QSqlQuery countQuery(db);
        int total = 0;
        if (countQuery.exec("SELECT COUNT(*) FROM logs") && countQuery.next()) {
            total = countQuery.value(0).toInt();
        } else {
            emit parseError("无法获取日志总数：" + countQuery.lastError().text());
            db.close();
            QSqlDatabase::removeDatabase("temp_parse_connection");
            delete logs;
            return nullptr;
        }

        logs->reserve(total);

        QSqlQuery query(db);
        if (!query.exec("SELECT id, priority, timestamp, content FROM logs")) {
            emit parseError("查询失败：" + query.lastError().text());
            db.close();
            QSqlDatabase::removeDatabase("temp_parse_connection");
            delete logs;
            return nullptr;
        }

        int processed = 0;
        while (query.next()) {
            qint64 id = query.value(0).toLongLong();
            int priority = query.value(1).toInt();
            qint64 timestamp = query.value(2).toLongLong();
            QString content = query.value(3).toString();
            logs->append(RecordData(id, priority, timestamp, content));
            processed++;
            if (processed % 500 == 0)
                emit parseProgress(processed, total);
        }

        db.close();
        // db 对象将在此处析构（离开作用域）
    }

    // 此时 db 已经销毁，可以安全移除连接
    QSqlDatabase::removeDatabase("temp_parse_connection");
    LOG_INFO(QString("解析数据库完成，共读取 %1 条日志").arg(logs->size()));
    return logs;
}

void RecordWorker::do_parseLogFile(const QString& filePath)
{
    QList<RecordData>* logs = parseLogFileContent(filePath);
    if (!logs || logs->isEmpty())
        emit parseError("文件解析后无有效日志记录");
    else
        emit parseFinished(logs);
}

void RecordWorker::do_parseDatabase(const QString& dbPath)
{
    QList<RecordData>* logs = parseDatabaseContent(dbPath);
    if (!logs || logs->isEmpty())
        emit parseError("数据库无日志记录");
    else
        emit parseFinished(logs);
}