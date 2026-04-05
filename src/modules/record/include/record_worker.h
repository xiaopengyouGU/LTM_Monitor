#ifndef RECORD_WORKER_H
#define RECORD_WORKER_H

#include <QObject>
#include <QtSql>

#if defined(RECORD_LIBRARY)
#  define RECORD_EXPORT Q_DECL_EXPORT
#else
#  define RECORD_EXPORT Q_DECL_IMPORT
#endif

class RecordManager;                   //前向声明，数据库管理器
class RecordData;                      //数据库数据
class QTimer;
class LogWorker;


class RECORD_EXPORT RecordWorker : public QObject{
    Q_OBJECT
public:
    explicit RecordWorker(QObject *parent = nullptr);
    ~RecordWorker();
    void connectLog(LogWorker *log);                        //绑定日志工作对象
signals:
    void dataBaseOpened(bool success, const QString& msg);  //数据库打开信号
    void dataBaseClose();                                   //数据库关闭信号 

    void parseFinished(QList<RecordData>* logs);            // 传递堆指针，接收方负责delete
    void parseError(const QString& error);                  
    void parseProgress(int current, int total);             

public slots:
    //增删查
    void insertData(const RecordData& data);
    void deleteData(const RecordData& data);
    void queryData(const RecordData& data);

    void start();                                              //启动定时器
    void stop();
    void do_logToDataBase(int level, const QString &message);  //将日志写入数据库
    void do_initDataBase(const QString& logName, const QString& dataName);                                            //停止定时器

    // 新增：解析文件/数据库
    void do_parseLogFile(const QString& filePath);
    void do_parseDatabase(const QString& dbPath);

private slots:
    void do_timer_timeout();                               //写入一批数据
private:
    void openLogDB(const QString& logName);
    void openDataDB(const QString& dataName);
    void writeLogNum();     
    void writeDataNum();

    QList<RecordData>* parseLogFileContent(const QString& filePath);
    QList<RecordData>* parseDatabaseContent(const QString& dbPath);

private:         
    QSqlDatabase data_DB;                                   //工艺数据库
    QSqlDatabase log_DB;                                    //日志数据库
    QTimer *m_timer;                                        //10s写入一批数据
    QList<RecordData> log_num;                              //日志数据列表
    QList<RecordData> data_num;                             //工艺数据列表
    // 
    LogWorker   *m_log;                                     //绑定日志工作对象                                       
};

#endif