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
    void open(SerialConfig config);                     // 打开串口
    void close();                                       // 关闭串口
    void send(uint8_t type, const QByteArray& data);    // 发送数据接口

    void startPeriodSend(uint8_t type, const QByteArray& data, int intervalMs);   // 周期发送
    void stopPeriodSend();
    void startModbus(const ModbusConfig &cfg);            // 启动 Modbus 轮询
    void stopModbus();                                    // 停止 Modbus 轮询
    void sendModbus(uint8_t slave, uint8_t func, uint16_t reg, const QByteArray &data);  // 用户层发送 Modbus 写类请求
    void setProtocol(uint8_t type);                       // 设置通讯协议：支持LTM协议、普通串口、Modbus RTU
signals:                                                  // 私有信号
    void openSerial(SerialConfig config);
    void closeSerial();
    void sendData(uint8_t type, const QByteArray& data);
    void protocolSet(uint8_t type);

    void periodSendData(uint8_t type, const QByteArray& data, int intervalMs);
    void stopPeriodSendData();
    void modbusStart(const ModbusConfig &cfg);
    void modbusStop();
    void modbusSend(uint8_t slave, uint8_t func, uint16_t reg, const QByteArray &data);
private slots:
    void do_serialDataUpdated(uint8_t data_type, const QByteArray& data);  // 数据更新
    void do_serialPortNumChanged(const QStringList& portNum);              // 端口数量变化  
    void do_serialOpened(bool success, const QString& msg);                // 串口打开信号
    void do_serialClose();
    void do_modbusResponse(uint8_t  addr, uint8_t func, uint16_t reg, const QByteArray& payload);  // Modbus 正常响应
    void do_modbusException(uint8_t addr, uint8_t func, uint8_t code);
    void do_modbusTimeout(int missCount);                                  // 响应超时（累计 miss）
private:
    SerialManager  *m_manager;          // 串口管理器对象
    QThread        *data_thread;        // 串口数据处理线程
    SerialWorker   *m_worker;           // 串口处理工作对象
};
