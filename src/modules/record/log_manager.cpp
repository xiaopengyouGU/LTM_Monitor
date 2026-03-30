#include "log_manager.h"
#include <QThread>

LogManager::~LogManager()
{
    spdlog::drop_all();         //清理所有的logger
}

LogManager& LogManager::instance()
{
    static LogManager instance;
    return instance; 
}

void LogManager::initialize(SqlManager * manager = nullptr)
{
    if(m_logger) return;            //避免重复初始化
    m_sql = manager;
    //1. 创建异步日志工厂
    spdlog::init_thread_pool(8192, 1);  //队列大小为8192 byte, 1个线程
    //2. 创建sinks
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::trace);      //全局日志级别
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%1%$] %v");
    
    //文件 sink : 最大5MB， 保留3个备份文件
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/app.log", 5*1024*1024, 3);
    file_sink->set_level(spdlog::level::trace);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%1] %v");

    //3.创建异步logger，高性能
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    m_logger = std::make_shared<spdlog::async_logger>("multi_sink",sinks.begin(),sinks.begin(), sinks.end(),
                                                        spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    m_logger->set_level(spdlog::level::trace);
    m_logger->flush_on(spdlog::level::warn);        //warn及以上立即刷新
    spdlog::register_logger(m_logger);
    spdlog::set_default_logger(m_logger);           //全局模式

    if(m_sql)   //连接信号
    {
        connect(this, &LogManager::logToDataBase, m_sql, &SqlManager::handleInsertLog, Qt::QueuedConnection);     
    }
}

void LogManager::debug(const QString& msg)
{
    if(m_logger)
        m_logger->debug(msg.toStdString());
    if(m_sql)
        emit logToDataBase(Log_Level_Debug, msg);
}

void LogManager::info(const QString& msg)
{
    if(m_logger)
        m_logger->info(msg.toStdString());
    if(m_sql)
        emit logToDataBase(Log_Level_Info, msg);
}

void LogManager::warn(const QString& msg)
{
    if(m_logger)
        m_logger->warn(msg.toStdString());
    if(m_sql)
        emit logToDataBase(Log_Level_Warn, msg);
}

void LogManager::error(const QString& msg)
{
    if(m_logger)
        m_logger->error(msg.toStdString());
    if(m_sql)
        emit logToDataBase(Log_Level_Error, msg);
}
