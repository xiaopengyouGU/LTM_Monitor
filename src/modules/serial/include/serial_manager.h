#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <QObject>

#if defined(SERIAL_LIBRARY)
#  define SERIAL_EXPORT Q_DECL_EXPORT
#else
#  define SERIAL_EXPORT Q_DECL_IMPORT
#endif

class SerialConfig{
public:
    uint8_t port;                       //端口号
    uint8_t check;                      //奇偶校验位
    uint8_t stop;                       //停止位
    uint8_t data;                       //数据位
    uint32_t baud;                      //波特率
    SerialConfig() {};                  //构造函数
};                                      //串口配置结构体

//串口管理器，负责进行串口相关操作，唯一与用户交互的模块
class SERIAL_EXPORT SerialManager:public QObject{
    Q_OBJECT
public:
    explicit SerialManager(QObject *parent = nullptr);
    ~SerialManager();
    void start();                                       //启动串口管理器
    void stop();                                        //停止串口管理器
    //串口相关操作接口
    void open(SerialConfig config);                     //打开串口
    void close();                                       //关闭串口
    void send(uint8_t type, const QByteArray& data);    //发送数据接口
signals:
    //发送给用户的信号
    void serialDataUpdated(uint8_t data_type, const QByteArray& data);  //数据更新
    void serialPortNumChanged(const QStringList& portNum);  //端口数量变化
    void serialOpened(bool success, const QString& msg); //串口打开信号
    void serialClose();                                  //串口关闭信号 
private:
    class Private;
    Private     *pimpl;                                 //采用Pimpl设计模式开发
};



#endif