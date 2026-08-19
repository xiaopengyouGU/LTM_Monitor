#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <QObject>

#if defined(SERIAL_LIBRARY)
#  define SERIAL_EXPORT Q_DECL_EXPORT
#else
#  define SERIAL_EXPORT Q_DECL_IMPORT
#endif

struct SerialConfig {
    uint8_t port;                       //端口号
    uint8_t check;                      //奇偶校验位
    uint8_t stop;                       //停止位
    uint8_t data;                       //数据位
    uint32_t baud;                      //波特率
};

// 串口管理器（传输层）：只做设备管理与字节收发，协议解析由上层组合
class SERIAL_EXPORT SerialManager:public QObject{
    Q_OBJECT
public:
    explicit SerialManager(QObject *parent = nullptr);
    ~SerialManager();
    void start();                                       // 启动串口管理器
    void stop();                                        // 停止串口管理器
    void open(const SerialConfig& config);              // 打开串口
    void close();                                       // 关闭串口
    void send(const QByteArray &bytes);                 // 发送原始字节
    void startPeriodSend(const QByteArray &bytes, int intervalMs);   // 周期发送原始字节
    void stopPeriodSend();                              // 停止周期发送
signals:
    void serialDataUpdated(const QByteArray &bytes);    // 接收原始字节
    void serialPortNumChanged(const QStringList& portNum); // 端口数量变化
    void serialOpened(bool success, const QString& msg);   // 串口打开信号
    void serialClose();
private:
    class Private;
    Private     *pimpl;                                 // 采用Pimpl设计模式开发
};
#endif
