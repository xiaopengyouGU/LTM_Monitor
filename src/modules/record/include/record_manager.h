#ifndef RECORD_MANAGER_H__
#define RECORD_MANAGER_H__

#include <QObject>
#include <QDateTime>
#include <QVariantMap>
#include <atomic>

#if defined(RECORD_LIBRARY)
#  define RECORD_EXPORT Q_DECL_EXPORT
#else
#  define RECORD_EXPORT Q_DECL_IMPORT
#endif

// 日志数据结构，用户可见
struct RECORD_EXPORT RecordData {
    qint64 id;          // 唯一标识符，自增，与时间戳有关
    int priority;       // 优先级，0最低，3最高
    qint64 timestamp;   // 时间戳，毫秒
    QString content;    // 内容
    QVariantMap extra;  // 扩展字段

    RecordData(int prior = 0, const QString& msg = "") {
        priority = prior;
        content = msg;
        static std::atomic<qint64> count{0};
        timestamp = QDateTime::currentMSecsSinceEpoch();
        id = count.fetch_add(1, std::memory_order_relaxed) + 2000 * timestamp;
    }
    RecordData(qint64 _id, int _priority, qint64 _timestamp, const QString& _content)
        : id(_id), priority(_priority), timestamp(_timestamp), content(_content) {}
    RecordData(const RecordData&) = default;
    RecordData& operator=(const RecordData&) = default;
};

class RECORD_EXPORT RecordManager : public QObject {
    Q_OBJECT
public:
    explicit RecordManager(QObject *parent = nullptr);
    ~RecordManager();
    //记录管理器公共接口
    void start();
    void stop();
    void setLogDB(const QString& fileName);                 //设置日志数据库名
    void setDataDB(const QString& fileName);                //设置工艺数据库名
    //日志操作接口
    void logDebug(const QString& msg);
    void logInfo(const QString& msg);
    void logWarn(const QString& msg);
    void logError(const QString& msg);
    //工艺数据库操作接口
    void createData(RecordData& data);                      //填充数据
    void insertData(const RecordData& data);                //插入数据
    void deleteData(const RecordData& data);                //删除数据
    void queryData(const RecordData& data);                 //查询数据
    //分析接口
    void parseLogFile(const QString& filePath);             //解析日志文件
    void parseDatabase(const QString& dbPath);              //解析数据库文件

signals:
    void dataBaseOpened(bool success, const QString& msg);
    void dataBaseClose(const QString& msg);
    void parseFinished(QList<RecordData>* logs);
    void parseError(const QString& error);
    void parseProgress(int current, int total);

private:
    class Private;
    Private *pimpl;
};

#endif // RECORD_MANAGER_H__