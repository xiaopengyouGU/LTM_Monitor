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
    //串口相关操作接口
    void open(SerialConfig config);                     //打开串口
    void close();                                       //关闭串口
    void send(uint8_t type, const QByteArray& data);    //发送数据接口
    void setProtocol(uint8_t type);                     //设置通讯协议：支持LTM协议和普通串口
signals:                                                //私有信号
    void openSerial(SerialConfig config);
    void closeSerial();
    void sendData(uint8_t type, const QByteArray& data);
    void protocolSet(uint8_t type);
private slots:
    void do_serialDataUpdated(uint8_t data_type, const QByteArray& data);  //数据更新
    void do_serialPortNumChanged(const QStringList& portNum);              //端口数量变化  
    void do_serialOpened(bool success, const QString& msg);                //串口打开信号
    void do_serialClose();                                                 //串口关闭信号 
private:
    SerialManager *m_manager;           //串口管理器对象
    QThread        *data_thread;        //串口数据处理线程
    SerialWorker   *m_worker;           //串口处理工作对象
};
