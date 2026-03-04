#ifndef __DATA_PROCESSOR_H__
#define __DATA_PROCESSOR_H__

#include <QObject>
#include <QSerialPort>
#include <QMutex>
#include <QThread>
#include <QDebug>
#include <atomic>
#include "protocol.h"
#include "serial.h"

//DataProcessor 负责处理协议解析与串口通讯功能，在data线程中执行任务
class DataProcessor : public QObject
{
    Q_OBJECT
public:
    explicit DataProcessor(QObject *parent = nullptr);
    ~DataProcessor();
    
    //配置串口，由主线程调用
    void setSerialPortConfig(serial_config_t *config);

public slots:   
    void do_sendData(uint8_t type, QByteArray data);
    void openSerial();   //打开串口
    void stop();         //停止处理，请求退出循环

private slots:
    void do_readyRead();                    //处理串口接收数据
signals:
    void dataReceived(uint8_t type, QByteArray data);  //解析完毕后的数据, 多线程中采用拷贝，避免潜在的数据覆盖等问题
    void serialStatusChanged(uint8_t flag); // 0: 打开成功， 1：关闭成功， 2：打开失败

private:
    QMutex m_mutex;             //保护串口配置的互斥锁
    std::atomic<bool> m_running;//控制循环的标志
    serial_config_t m_config;   //串口配置，值类型
};

#endif