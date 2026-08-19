#ifndef SERIAL_WORKER_H
#define SERIAL_WORKER_H

#include <QObject>
#include <QByteArray>

#if defined(SERIAL_LIBRARY)
#  define SERIAL_EXPORT Q_DECL_EXPORT
#else
#  define SERIAL_EXPORT Q_DECL_IMPORT
#endif

class SerialManager;
class SerialConfig;
class QSerialPort;
class QTimer;

// 串口传输 Worker：只做物理层——开关设备、字节收发、周期发送、端口轮询
class SERIAL_EXPORT SerialWorker : public QObject
{
    Q_OBJECT
public:
    explicit SerialWorker(QObject *parent = nullptr);
    ~SerialWorker();

signals:
    void serialDataUpdated(const QByteArray &bytes);        // 接收原始字节
    void serialPortNumChanged(const QStringList &portNum);  // 端口数量变化
    void serialOpened(bool success, const QString &msg);    // 打开串口信号
    void serialClose();

public slots:
    void open(const SerialConfig& config);
    void close();
    void send(const QByteArray &bytes);                     // 发送原始字节
    void start();                                           // 启动端口轮询定时器
    void stop();
    void startPeriodSend(const QByteArray &bytes, int intervalMs);   // 周期发送原始字节
    void stopPeriodSend();

private slots:
    void do_readyRead();
    void do_timer_timeout();
    void do_sendTimer_timeout();

private:
    QStringList m_lastPorts;                        // 记录的端口信息
    QSerialPort *comPort = nullptr;                 // 串口对象
    QTimer      *m_timer = nullptr;                 // 端口检测定时器，1500ms 检查一次
    QTimer      *m_sendTimer = nullptr;             // 周期发送定时器（PreciseTimer，Worker 线程）
    QByteArray   m_sendData;                        // 周期发送的字节
};

#endif // SERIAL_WORKER_H
