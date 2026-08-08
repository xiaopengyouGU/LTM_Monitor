#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <QObject>
#include "serial_def.h"

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
// 串口管理器，负责进行串口相关操作，唯一与用户交互的模块
class SERIAL_EXPORT SerialManager:public QObject{
    Q_OBJECT
public:
    explicit SerialManager(QObject *parent = nullptr);
    ~SerialManager();
    void start();                                       // 启动串口管理器
    void stop();                                        // 停止串口管理器
    void open(SerialConfig config);                     // 打开串口
    void close();                                       // 关闭串口
    void send(uint8_t type, const QByteArray& data);    // 发送数据接口

    void startPeriodSend(uint8_t type, const QByteArray& data, int intervalMs);  // 周期发送：Worker 线程定时器
    void stopPeriodSend();
    void startModbus(const ModbusConfig &cfg);             // 启动 Modbus 轮询（固定读指令）
    void stopModbus();                                     // 停止 Modbus 轮询
    void sendModbus(uint8_t slave, uint8_t func, uint16_t reg, const QByteArray &data);  // 用户层发送 Modbus 写类请求（协议层组帧+CRC，插队）
    void setProtocol(uint8_t type);                        // 设置通讯协议：支持 LTM协议（0） 、普通串口（1）、Modbus RTU（2）
signals:
    //发送给用户的信号
    void serialDataUpdated(uint8_t data_type, const QByteArray& data);  // 数据更新
    void serialPortNumChanged(const QStringList& portNum); // 端口数量变化
    void serialOpened(bool success, const QString& msg);   // 串口打开信号
    void serialClose();
    void modbusResponse(uint8_t  addr, uint8_t func, uint16_t reg, const QByteArray& payload);  // Modbus 正常响应
    void modbusException(uint8_t addr, uint8_t func, uint8_t code);               // Modbus 异常响应
    void modbusTimeout(int missCount);                                            // 响应超时（累计 miss）
private:
    class Private;
    Private     *pimpl;                                     // 采用Pimpl设计模式开发
};



#endif
