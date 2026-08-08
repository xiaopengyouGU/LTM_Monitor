#include "serial_manager.h"
#include "serial_worker.h"
#include "serial_protocol.h"
#include "serial_def.h"
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>

SerialWorker::SerialWorker(QObject *parent): QObject(parent)
{
    comPort  = new QSerialPort(this);       // 内存管理交给Qt
    protocol = new SerialProtocol();       
    m_timer  = new QTimer(this);            // 端口数检测定时器，1500ms刷新一次
    m_lastPorts = {};
    // 初始化协议解析对象
    protocol->init();
    m_type = 0;                            // 默认采用 LTM协议
    // 热插拔检测定时器初始化
    m_timer->setTimerType(Qt::CoarseTimer);
    m_timer->setInterval(1500);            // 1.5s 检测一次端口
    m_timer->setSingleShot(false);
    m_timer->stop();
    // 绑定回调函数
    connect(m_timer, &QTimer::timeout, this, &SerialWorker::do_timer_timeout);

    // 周期发送定时器：PreciseTimer，工作在 Worker 线程，发送时序不受 UI 线程影响
    m_sendTimer = new QTimer(this);
    m_sendTimer->setTimerType(Qt::PreciseTimer);
    m_sendTimer->setSingleShot(false);
    m_sendTimer->stop();
    connect(m_sendTimer, &QTimer::timeout, this, &SerialWorker::do_sendTimer_timeout);

    // Modbus 轮询定时器：PreciseTimer，Worker 线程
    m_pollTimer = new QTimer(this);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    m_pollTimer->setSingleShot(false);
    m_pollTimer->stop();
    connect(m_pollTimer, &QTimer::timeout, this, &SerialWorker::do_pollTimer_timeout);

    // Modbus 响应看门狗：单次，超时 300ms
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(300);
    m_watchdog->stop();
    connect(m_watchdog, &QTimer::timeout, this, &SerialWorker::do_watchdog_timeout);
}

SerialWorker::~SerialWorker()
{
    close();                                // 手动关闭串口
    delete protocol;                        // 手动析构
}

void SerialWorker::open(SerialConfig config)
{
    QList<QSerialPortInfo> com_list = QSerialPortInfo::availablePorts();
    if (config.port >= com_list.size()) { 
        emit serialOpened(false, "未找到串口"); 
        return; 
    }
    QSerialPortInfo port_info = com_list.at(config.port);
    comPort->setPort(port_info);             // 设置端口
    // 设置串口通信参数
    comPort->setBaudRate(config.baud);
    comPort->setDataBits(QSerialPort::DataBits(config.data));
    comPort->setStopBits(QSerialPort::StopBits(config.stop));
    comPort->setParity(QSerialPort::Parity(config.check));
    // 打开串口
    bool res = comPort->open(QIODeviceBase::ReadWrite);
    if (res) {
        connect(comPort, &QSerialPort::readyRead, this, &SerialWorker::do_readyRead);
        emit serialOpened(true, "串口打开成功");
    }
    else    emit serialOpened(false, "串口打开失败");
}

void SerialWorker::close()
{
    m_sendTimer->stop();                        // 关闭串口时停止周期发送
    m_pollTimer->stop();                        // 关闭串口时停止 Modbus 轮询
    m_watchdog->stop();
    m_modbusQueue.clear();
    m_modbusBusy = false;                       
    if(comPort->isOpen()) {
        //断开连接
        disconnect(comPort, &QSerialPort::readyRead, this, &SerialWorker::do_readyRead);
        comPort->close();
        protocol->init();                       // 清空环形缓冲区，避免旧数据影响
        emit serialClose();
    }
}

void SerialWorker::setProtocol(uint8_t type)     // 设置通讯协议
{
    if (m_type != type){
        m_type = type;
        protocol->init();                        // 立即清空缓冲区
    }
    if (m_type != Prot_Modbus)  stopModbus();    // 切出 Modbus 协议时停止轮询
    else                        stopPeriodSend();// Modbus 协议下，停止周期发送接口
}

void SerialWorker::start()                       // 启动定时器
{
    m_timer->start();
}

void SerialWorker::stop()                        // 停止定时器
{
    m_timer->stop();
}                                          

void SerialWorker::do_readyRead()
{   // 进入数据读取循环
    uint8_t type;
    QByteArray rawData = comPort->readAll();   // 获取串口缓冲区数据
    QByteArray data;                           // 处理完后的数据
    if (m_type == Prot_Common) {                         // 当前通讯协议为：普通串口
        emit serialDataUpdated(Data_CMD_Text, rawData);  // 可认为接收的均是Text数据，
        return;
    } else if (m_type == Prot_Modbus){         // Modbus RTU 通讯协议
        if (!m_modbusBusy)   return;           // 空闲时不接字节，避免过期配置参与解析
        // 与 LTM 同构：receive 入环缓冲，process 循环取帧
        protocol->receive(rawData);            // 判断从机是否成功响应
        while (protocol->process(m_modbusPendingCfg, data)) {
            if (m_modbusPendingCfg.func & 0x80)
                emit modbusException(m_modbusPendingCfg.slave, m_modbusPendingCfg.func, (uint8_t)data.at(0));
            else
                emit modbusResponse(m_modbusPendingCfg.slave, m_modbusPendingCfg.func, m_modbusPendingCfg.startReg, data);
            m_watchdog->stop();                // 喂狗
            m_modbusBusy = false;
            modbusPump();                      // 尝试发送一帧数据
            return;
        }
        return;
    }

    // LTM 通讯协议
    protocol->receive(rawData);                 // 将接收的数据放入环形缓冲区
    // 循环解析数据帧
    while (1) {
        if (protocol->process(type, data)) {    // 接收到完整的数据帧
            emit serialDataUpdated(type, data); // 利用了Qt的隐式共享机制，减少后续拷贝的花销
        }
        else break;                             // 没有完整帧，退出循环
    }
}

void SerialWorker::do_timer_timeout()
{
    QStringList currentPorts;
    foreach (const QSerialPortInfo &portInfo, QSerialPortInfo::availablePorts()) {
        currentPorts << portInfo.portName() + ":" + portInfo.description();
    }
    // 比较是否与上次相同
    if (currentPorts == m_lastPorts)
        return;
    // 保存当前列表
    m_lastPorts = currentPorts;
    emit serialPortNumChanged(m_lastPorts);      // 发送端口信息给UI主线程    
}

void SerialWorker::send(uint8_t type, const QByteArray& data)   // 发送数据接口
{
    if(comPort->isOpen()) {                   
        if (m_type == Prot_Modbus)     return;   // Modbus RTU协议不走该接口
        if (m_type == Prot_Common) {             // 采用普通串口
            comPort->write(data);                // 直接发送！
            return;
        }
        protocol->package(type, data);           // 将数据打包（LTM协议）
        comPort->write(protocol->getRawData());  // 发送打包完后的原始数据
    }
}

void SerialWorker::startPeriodSend(uint8_t type, const QByteArray& data, int intervalMs)    // 周期发送接口
{
    m_sendTimer->stop();
    m_sendType = type;
    m_sendData = data;
    if (intervalMs <= 0)                        return;    // 非法间隔，仅停止原任务
    m_sendTimer->setInterval(intervalMs);
    m_sendTimer->start();
}

void SerialWorker::stopPeriodSend()                                                           // 停止周期发送
{
    m_sendTimer->stop();
}

void SerialWorker::do_sendTimer_timeout()                                                     // 周期发送定时器回调
{
    send(m_sendType, m_sendData);
}

// ==================== Modbus RTU 主站实现 ====================

// 启动轮询：立即发第一问，之后按间隔入队固定读指令（去重：在途或队列非空则跳过）
void SerialWorker::startModbus(const ModbusConfig &cfg)
{
    if (m_type != Prot_Modbus)          return;  // 当前协议对比，直接返回
 
    m_modbusCfg = cfg;
    m_modbusPollOn = true;                       // 标记轮询状态  
    m_modbusMiss = 0;                            // 重新开始计数
    m_pollTimer->start(cfg.intervalMs);
    QByteArray qty;                              // 读请求的数据区 = 数量（2 字节大端）
    qty.append((char)(cfg.quantity >> 8));
    qty.append((char)(cfg.quantity & 0xFF));
    modbusEnqueue(cfg, qty, false);
}

void SerialWorker::stopModbus()
{
    m_modbusPollOn = false;
    m_pollTimer->stop();
    m_watchdog->stop();
    m_modbusQueue.clear();
    m_modbusBusy = false;
    protocol->init();                         // 清空 Modbus 环缓冲
}

// 用户层发送 Modbus 写类请求：协议层组帧+CRC，插队到队首
void SerialWorker::sendModbus(uint8_t slave, uint8_t func, uint16_t reg, const QByteArray &data)
{
    ModbusConfig cfg;
    cfg.slave    = slave;
    cfg.func     = func;
    cfg.startReg = reg;
    // 读类请求的数量从数据区前两字节取（大端），写类用不上
    if (data.size() < 2)    return;   // 数据过少，忽略本次写入
    if (func == 0x01 || func == 0x02 || func == 0x03 || func == 0x04)
        cfg.quantity = ((uint8_t)data.at(0) << 8) | (uint8_t)data.at(1);
    modbusEnqueue(cfg, data, true);   // 数据帧构建并立即入队
}

void SerialWorker::modbusEnqueue(const ModbusConfig &cfg, const QByteArray &payload, bool front)
{
    ModbusTx it;
    it.cfg     = cfg;
    it.payload = payload;
    if (front)  m_modbusQueue.push_front(it);
    else        m_modbusQueue.push_back(it);
    modbusPump();                                   // 尝试触发一次帧发送
}

void SerialWorker::do_pollTimer_timeout()
{
    if (!m_modbusPollOn || m_modbusBusy || !m_modbusQueue.empty())
        return;                                     // 去重：在途或队列非空则跳过
    QByteArray qty;
    qty.append((char)(m_modbusCfg.quantity >> 8));
    qty.append((char)(m_modbusCfg.quantity & 0xFF));
    modbusEnqueue(m_modbusCfg, qty, false);   // 发送帧入队
}

void SerialWorker::modbusPump()
{
    if (m_modbusBusy || m_modbusQueue.empty())      // Modbus 总线忙碌中或队列为空
        return;
    m_modbusPending = m_modbusQueue.front();
    m_modbusQueue.pop_front();                      // 事务出队 
    m_modbusPendingCfg = m_modbusPending.cfg;
    m_modbusBusy = true;                            // 标记总线忙碌，等待从机响应完毕
    m_watchdog->start();                            // 掉线看门狗启动

    protocol->package(m_modbusPendingCfg, m_modbusPending.payload);
    comPort->write(protocol->getRawData());         // 只经事务泵，不走 send() 门禁
}

void SerialWorker::do_watchdog_timeout()
{
    m_modbusMiss++;
    emit modbusTimeout(m_modbusMiss);              // 看门狗超时
    m_modbusBusy = false;                          // 总线强制释放

    modbusPump();
}

