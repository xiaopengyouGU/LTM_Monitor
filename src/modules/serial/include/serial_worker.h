#ifndef SERIAL_WORKER_H
#define SERIAL_WORKER_H

#include <QObject>
#include <QByteArray>
#include <deque>
#include "serial_def.h"

#if defined(SERIAL_LIBRARY)
#  define SERIAL_EXPORT Q_DECL_EXPORT
#else
#  define SERIAL_EXPORT Q_DECL_IMPORT
#endif

class SerialManager;
class SerialConfig;
class SerialProtocol;             // 协议解析对象
class QSerialPort;                // 串口对象
class QTimer;

class SERIAL_EXPORT SerialWorker : public QObject{
    Q_OBJECT
public:
    explicit SerialWorker(QObject *parent = nullptr);
    ~SerialWorker();
signals:
    void serialDataUpdated(uint8_t data_type, const QByteArray& data);
    void serialPortNumChanged(const QStringList& portNum);  // 端口数量变化
    void serialOpened(bool success, const QString& msg);    // 串口打开信号
    void serialClose();
    void modbusResponse(uint8_t addr, uint8_t func, uint16_t reg, const QByteArray& payload);   // Modbus 正常响应（带起始寄存器）
    void modbusException(uint8_t addr, uint8_t func, uint8_t code);               // Modbus 异常响应
    void modbusTimeout(int missCount);                                            // 响应超时（累计 miss）

public slots:
    void open(SerialConfig config);
    void close();
    void send(uint8_t type, const QByteArray& data);
    void setProtocol(uint8_t type);                         // 设置通讯协议
    void start();                                           // 启动定时器
    void stop();                                            // 停止定时器

    void startPeriodSend(uint8_t type, const QByteArray& data, int intervalMs);   // 周期发送：保存数据并按间隔重发（不立即发送）
    void stopPeriodSend();
    void startModbus(const ModbusConfig &cfg);              // 启动 Modbus 轮询（固定读指令）
    void stopModbus();                                      // 停止 Modbus 轮询
    void sendModbus(uint8_t slave, uint8_t func, uint16_t reg, const QByteArray &data);  // 用户层发送 Modbus 写类请求（协议层组帧+CRC，插队）
private slots:
    void do_readyRead();
    void do_timer_timeout();
    void do_sendTimer_timeout();
    void do_pollTimer_timeout();                              // Modbus 轮询定时器
    void do_watchdog_timeout();                               // Modbus 响应看门狗

private:
    QStringList     m_lastPorts;                      // 记录的端口信息
    QSerialPort     *comPort;                         // 串口对象
    SerialProtocol  *protocol;                        // 协议解析对象
    QTimer          *m_timer;                         // 定时器，1500ms查询一次端口数量信息

    QTimer          *m_sendTimer;                     // 周期发送定时器（PreciseTimer，Worker 线程）
    uint8_t         m_sendType = 0;                   // 周期发送的数据类型
    QByteArray      m_sendData;                       // 周期发送的数据
    struct ModbusTx {
        ModbusConfig cfg;         // 在途事务上下文（伪帧头定界/校验）
        QByteArray   payload;     // 数据区（读=数量2字节，写=值/值序列）
    };
    // Modbus RTU 相关变量
    QTimer          *m_pollTimer = nullptr;      // Modbus 轮询定时器（PreciseTimer）
    QTimer          *m_watchdog  = nullptr;      // 响应看门狗
    std::deque<ModbusTx> m_modbusQueue;          // 事务队列（轮询 + 用户帧统一排队）
    ModbusTx        m_modbusPending;             // 当前在途事务
    ModbusConfig    m_modbusPendingCfg;          // 当前在途事务的配置
    ModbusConfig    m_modbusCfg;                 // 主机轮询配置
    bool            m_modbusPollOn = false;      // 轮询标志位
    bool            m_modbusBusy   = false;      // 总线忙碌标志位
    int             m_modbusMiss   = 0;          // 累计 miss 计数
    void modbusPump();                           // 事务泵：idle 出队发送
    void modbusEnqueue(const ModbusConfig &cfg, const QByteArray &payload, bool front);
    uint8_t         m_type;                      // 支持的串口协议（0：LTM协议、1：普通串口、2：Modbus RTU)
};

#endif
