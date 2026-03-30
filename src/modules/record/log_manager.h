// #ifndef __LOG_MANAGER_H__
// #define __LOG_MANAGER_H__

// #include <QObject>
// #include <QString>
// #include <memory>
// #include "spdlog.h"
// #include "async.h"
// #include "sinks/stdout_color_sinks.h"
// #include "sinks/rotating_file_sink.h"
// //日志管理器对象负责处理系统日志操作（采用异步写入和单例模式）

// class SqlManager;       //前向声明, 提高编译速度

// class LogManager : public QObject{
//     Q_OBJECT
// private:
//     static LogManager& instance();  //单例访问
// public:
//     //初始化日志系统，必须在主线程中调用
//     void initialize(SqlManager * manager = nullptr);
//     //日志接口
//     void debug(const QString& msg);
//     void info(const QString& msg);
//     void warn(const QString& msg);
//     void error(const QString& msg);

// signals:
//     void logToDataBase(int level, const QString& msg);      //发送日志等级和内容到数据库

// private:
//     explicit LogManager(QObject* parent = nullptr):QObject(parent){};
//     ~LogManager();
//     LogManager& operator=(const LogManager&) = delete;

//     std::shared_ptr<spdlog::logger> m_logger;               //创建智能指针
//     SqlManager* m_sql = nullptr;
// };

// //全局调用宏
// #define LOG_DEBUG(msg)      LogManager::instance().debug(msg)
// #define LOG_INFO(msg)       LogManager::instance().info(msg)
// #define LOG_WARN(msg)       LogManager::instance().warn(msg)
// #define LOG_ERROR(msg)      LogManager::instance().error(msg)

// //定义日志级别
// typedef enum
// {
//     Log_Level_Debug = 0,
//     Log_Level_Info,
//     Log_Level_Warn,
//     Log_Level_Error
// }Log_Level;



//#endif