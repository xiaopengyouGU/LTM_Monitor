#ifndef SERIAL_WORKER_H
#define SERIAL_WORKER_H

#include <QObject>

#if defined(SERIAL_LIBRARY)
#  define SERIAL_EXPORT Q_DECL_EXPORT
#else
#  define SERIAL_EXPORT Q_DECL_IMPORT
#endif

class SerialManager;
class SerialConfig;
class SerialProtocol;             //协议解析对象
class QSerialPort;                //串口对象
class QTimer;

class SERIAL_EXPORT SerialWorker : public QObject{
    Q_OBJECT
public:
    explicit SerialWorker(QObject *parent = nullptr);
    ~SerialWorker();
signals:
    void serialDataUpdated(uint8_t data_type, const QByteArray& data);
    void serialPortNumChanged(const QStringList& portNum);  //端口数量变化
    void serialOpened(bool success, const QString& msg);    //串口打开信号
    void serialClose();                                     //串口关闭信号 

public slots:
    void open(SerialConfig config);
    void close();
    void send(uint8_t type, const QByteArray& data);
    void start();                                           //启动定时器
    void stop();                                            //停止定时器
private slots:
    void do_readyRead();
    void do_timer_timeout();

private:
    QStringList     m_lastPorts;                       //记录的端口信息
    QSerialPort     *comPort;                          //串口对象
    SerialProtocol  *protocol;                         //协议解析对象
    QTimer          *m_timer;                          //定时器，1500ms查询一次端口数量信息         
};

#endif