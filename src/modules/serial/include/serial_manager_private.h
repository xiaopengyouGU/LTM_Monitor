#include "serial_manager.h"

#if defined(SERIAL_LIBRARY)
#  define SERIAL_EXPORT Q_DECL_EXPORT
#else
#  define SERIAL_EXPORT Q_DECL_IMPORT
#endif

//前向声明
class SerialWorker;                     //串口工作对象
class QThread;
class QSerialPortInfo;

class SERIAL_EXPORT SerialManager::Private : public QObject{
    Q_OBJECT
public:
    Private(SerialManager *parent);
    ~Private();
    void start();
    void stop();
    void open(const SerialConfig& config);              // 打开串口
    void close();                                       // 关闭串口
    void send(const QByteArray &bytes);                 // 发送原始字节
    void startPeriodSend(const QByteArray &bytes, int intervalMs);   // 周期发送
    void stopPeriodSend();
signals:                                                // 私有信号
    void openSerial(const SerialConfig& config);
    void closeSerial();
    void sendData(const QByteArray &bytes);
    void periodSendData(const QByteArray &bytes, int intervalMs);
    void stopPeriodSendData();
private slots:
    void do_serialDataUpdated(const QByteArray &bytes);                 // 数据更新
    void do_serialPortNumChanged(const QStringList& portNum);           // 端口数量变化
    void do_serialOpened(bool success, const QString& msg);             // 串口打开信号
    void do_serialClose();
private:
    SerialManager  *m_manager;          // 串口管理器对象
    QThread        *data_thread;        // 串口数据处理线程
    SerialWorker   *m_worker;           // 串口处理工作对象
};
