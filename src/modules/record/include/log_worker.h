#ifndef LOG_WORKER_H
#define LOG_WORKER_H

#include <QObject>
#include <QString>
#include <memory>

//前向声明
namespace spdlog {
    class logger;
}

#if defined(RECORD_LIBRARY)
#  define RECORD_EXPORT Q_DECL_EXPORT
#else
#  define RECORD_EXPORT Q_DECL_IMPORT
#endif

typedef enum {
    Log_Level_Debug = 0,
    Log_Level_Info,
    Log_Level_Warn,
    Log_Level_Error
} Log_Level;

class QTimer;

class RECORD_EXPORT LogWorker : public QObject
{
    Q_OBJECT
public:
    explicit LogWorker(QObject *parent = nullptr);
    ~LogWorker();

    void start();                        
    void stop();                       
    void debug(const QString& msg);
    void info(const QString& msg);
    void warn(const QString& msg);
    void error(const QString& msg);

signals:
    void logToDataBase(int level, const QString& msg);   // 发送给 RecordWorker 

private:
    void checkAndRotate();              // 跨天切换日志文件

    std::shared_ptr<spdlog::logger> m_logger;
};

#endif // LOG_WORKER_H