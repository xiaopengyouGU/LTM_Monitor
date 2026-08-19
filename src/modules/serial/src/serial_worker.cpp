#include "serial_worker.h"
#include "serial_manager.h"
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>

SerialWorker::SerialWorker(QObject *parent) : QObject(parent)
{
    comPort  = new QSerialPort(this);               // 内存管理交给Qt
    m_timer  = new QTimer(this);                    // 端口数检测定时器
    m_timer->setTimerType(Qt::CoarseTimer);
    m_timer->setInterval(1500);                     // 1.5s 检测一次端口
    m_timer->setSingleShot(false);
    m_timer->stop();
    connect(m_timer, &QTimer::timeout, this, &SerialWorker::do_timer_timeout);

    m_sendTimer = new QTimer(this);                 // 周期发送定时器：PreciseTimer，Worker 线程
    m_sendTimer->setTimerType(Qt::PreciseTimer);
    m_sendTimer->setSingleShot(false);
    m_sendTimer->stop();
    connect(m_sendTimer, &QTimer::timeout, this, &SerialWorker::do_sendTimer_timeout);
}

SerialWorker::~SerialWorker()
{
    close();                                        // 手动关闭串口
}

void SerialWorker::open(const SerialConfig &config)
{
    QList<QSerialPortInfo> com_list = QSerialPortInfo::availablePorts();
    if (config.port >= com_list.size()) {
        emit serialOpened(false, "未找到串口");
        return;
    }
    QSerialPortInfo port_info = com_list.at(config.port);
    comPort->setPort(port_info);                    // 设置端口
    comPort->setBaudRate(config.baud);
    comPort->setDataBits(QSerialPort::DataBits(config.data));
    comPort->setStopBits(QSerialPort::StopBits(config.stop));
    comPort->setParity(QSerialPort::Parity(config.check));
    if (comPort->open(QIODeviceBase::ReadWrite)) {
        connect(comPort, &QSerialPort::readyRead, this, &SerialWorker::do_readyRead);
        emit serialOpened(true, "串口打开成功");
    } else {
        emit serialOpened(false, "串口打开失败");
    }
}

void SerialWorker::close()
{
    m_sendTimer->stop();                            // 关闭时停止周期发送
    if (comPort->isOpen()) {
        disconnect(comPort, &QSerialPort::readyRead, this, &SerialWorker::do_readyRead);
        comPort->close();
        emit serialClose();
    }
}

void SerialWorker::send(const QByteArray &bytes)    // 发送原始字节
{
    if (comPort->isOpen() && !bytes.isEmpty())
        comPort->write(bytes);
}

void SerialWorker::do_readyRead()
{
    emit serialDataUpdated(comPort->readAll());     // 原始字节上报，协议解析由上层完成
}

void SerialWorker::start()                          // 启动端口轮询
{
    m_timer->start();
}

void SerialWorker::stop()
{
    m_timer->stop();
}

void SerialWorker::startPeriodSend(const QByteArray &bytes, int intervalMs)
{
    m_sendTimer->stop();
    m_sendData = bytes;
    if (intervalMs <= 0 || bytes.isEmpty())
        return;                                     // 非法参数，仅停止原任务
    m_sendTimer->setInterval(intervalMs);
    m_sendTimer->start();
}

void SerialWorker::stopPeriodSend()
{
    m_sendTimer->stop();
}

void SerialWorker::do_sendTimer_timeout()
{
    send(m_sendData);
}

void SerialWorker::do_timer_timeout()
{
    QStringList currentPorts;
    foreach (const QSerialPortInfo &portInfo, QSerialPortInfo::availablePorts()) {
        currentPorts << portInfo.portName() + ":" + portInfo.description();
    }
    if (currentPorts != m_lastPorts) {
        m_lastPorts = currentPorts;
        emit serialPortNumChanged(m_lastPorts);
    }
}
