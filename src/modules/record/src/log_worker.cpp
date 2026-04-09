#include "log_worker.h"
#include "spdlog/async.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <QDateTime>
#include <QDir>
#include <QTimer>
#include <QApplication>

static std::string getTodayLogFileName()
{
    // 获取可执行文件所在目录: LTM_APP/XXX_Project/bin/
    QString appDir = QCoreApplication::applicationDirPath();
    // 向上两级: LTM_APP/
    QDir logDir(appDir);
    logDir.cdUp();      // LTM_APP/LTM_Project/
    logDir.cdUp();      // LTM_APP/

    // 创建 logs 子目录（如果不存在）
    if (!logDir.exists("logs")) {
        logDir.mkdir("logs");
    }
    
    // 进入 logs 子目录
    logDir.cd("logs");

    // 格式: LTM_APP/logs/app_2026-04-01.log
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString fileName = logDir.filePath(QString("app_%1.log").arg(date));

    return fileName.toStdString();
}

LogWorker::LogWorker(QObject *parent) : QObject(parent)
{
    // 1. 创建异步日志工厂
    spdlog::init_thread_pool(8192, 1);  // 队列大小为8192 bytes, 1个线程

    // 2. 创建 sinks(接收器)
    std::vector<spdlog::sink_ptr> sinks;
    // 文件日志：无大小限制，永不过期（一天基本不会超过20MB,问题不大）
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        getTodayLogFileName(),
        false                // 追加模式
    );
    // 手动确保文件指针在末尾
    file_sink->flush();
    file_sink->set_level(spdlog::level::trace);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    sinks.push_back(file_sink);

#ifdef QT_DEBUG
    // 调试模式：加上控制台输出
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::trace);
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    sinks.push_back(console_sink);
#endif

    // 3. 创建异步logger，高性能
    m_logger = std::make_shared<spdlog::async_logger>(
        "multi_sink",
        sinks.begin(),
        sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::block
    );

    // 设置日志级别（低于该级别的日志直接忽略掉）
#ifdef QT_DEBUG
    m_logger->set_level(spdlog::level::debug);
#else
    m_logger->set_level(spdlog::level::info);
#endif

    m_logger->flush_on(spdlog::level::warn);   // warn及以上立即刷新
    spdlog::register_logger(m_logger);
    spdlog::set_default_logger(m_logger);      // 全局模式（可选，便于其他模块使用）

    // 跨天时，自动切换日志文件
}

LogWorker::~LogWorker()
{
    spdlog::shutdown();   // 关闭异步线程,这是非常关键的一步
    spdlog::drop_all();   // 清理所有的logger
}

void LogWorker::start()
{
}

void LogWorker::stop()
{
}

void LogWorker::checkAndRotate()
{
    if (!m_logger) return;

    static QString currentDate = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");

    // 日期变了，立即切换
    if (currentDate != today)
    {
        currentDate = today;
        // 获取新文件名
        std::string newFileName = getTodayLogFileName();
        // 创建新的 file sink
        auto new_file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            newFileName,
            false  // 追加模式
        );
        
        new_file_sink->set_level(spdlog::level::trace);
        new_file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

        // 构建新的 sinks 列表
        std::vector<spdlog::sink_ptr> new_sinks;
        new_sinks.push_back(new_file_sink);
#ifdef QT_DEBUG
        // 调试模式：保留控制台输出
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::trace);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        new_sinks.push_back(console_sink);
#endif
        // 清空旧的 sinks，添加新的 sinks
        m_logger->sinks().clear();
        for (const auto& sink : new_sinks) {
            m_logger->sinks().push_back(sink);
        }

        info(QString("切换到新日志文件: app_%1.log").arg(today));
    }
}

void LogWorker::debug(const QString& msg)
{
    if (m_logger)
        m_logger->debug(msg.toStdString());
    emit logToDataBase(Log_Level_Debug, msg);
    checkAndRotate();
}

void LogWorker::info(const QString& msg)
{
    if (m_logger)
        m_logger->info(msg.toStdString());
    emit logToDataBase(Log_Level_Info, msg);
    checkAndRotate();
}

void LogWorker::warn(const QString& msg)
{
    if (m_logger)
        m_logger->warn(msg.toStdString());
    emit logToDataBase(Log_Level_Warn, msg);
    checkAndRotate();
}

void LogWorker::error(const QString& msg)
{
    if (m_logger)
        m_logger->error(msg.toStdString());
    emit logToDataBase(Log_Level_Error, msg);
    checkAndRotate();
}