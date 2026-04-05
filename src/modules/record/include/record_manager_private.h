#ifndef RECORD_MANAGER_PRIVATE_H
#define RECORD_MANAGER_PRIVATE_H

#include "record_manager.h"
#include <QThread>
#include <QString>

class RecordWorker;
class LogWorker;                    //日志工作对象

#if defined(RECORD_LIBRARY)
#  define RECORD_EXPORT Q_DECL_EXPORT
#else
#  define RECORD_EXPORT Q_DECL_IMPORT
#endif


class RECORD_EXPORT RecordManager::Private : public QObject {
    Q_OBJECT
public:
    explicit Private(RecordManager *parent);
    ~Private();

    void start();
    void stop();
    void setLogDB(const QString& fileName);
    void setDataDB(const QString& fileName);
    //日志操作接口
    void logDebug(const QString& msg);
    void logInfo(const QString& msg);
    void logWarn(const QString& msg);
    void logError(const QString& msg);
    //工艺数据库操作接口
    void createData(RecordData& data);
    void insertData(const RecordData& data);
    void deleteData(const RecordData& data);
    void queryData(const RecordData& data);
    void parseLogFile(const QString& filePath);
    void parseDatabase(const QString& dbPath);

private slots:
    // 转发 RecordWorker 信号到 RecordManager（使用 do_ 前缀）
    void do_parseFinished(QList<RecordData>* logs);
    void do_parseError(const QString& error);
    void do_parseProgress(int current, int total);
    void do_dataBaseOpened(bool success, const QString& msg);
    void do_dataBaseClose();

signals:
    // 内部信号，用于跨线程调用 RecordWorker
    void initDataBase(const QString& logName, const QString& dataName);
    void logToDataBase(int level, const QString& message);
    void dataInsert(const RecordData& data);
    void dataDelete(const RecordData& data);
    void dataQuery(const RecordData& data);
    void parseLogFileSig(const QString& filePath);
    void parseDatabaseSig(const QString& dbPath);

private:
    RecordManager *m_manager;
    QThread *record_thread;
    RecordWorker *m_worker;
    LogWorker    *m_log;
    QString logDB_name;
    QString dataDB_name;
};

#endif // RECORD_MANAGER_PRIVATE_H