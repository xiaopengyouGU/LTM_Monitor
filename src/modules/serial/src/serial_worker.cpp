#include "serial_manager.h"
#include "serial_worker.h"
#include "serial_protocol.h"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>

SerialWorker::SerialWorker(QObject *parent):QObject(parent)
{
    comPort = new QSerialPort(this);        //内存管理交给Qt
    protocol = new SerialProtocol();       
    m_timer = new QTimer(this);            //端口数检测定时器，1500ms刷新一次
    m_lastPorts = {};
    //初始化协议解析对象
    protocol->init();                      //
    //定时器初始化
    m_timer->setTimerType(Qt::CoarseTimer);
    m_timer->setInterval(1500);
    m_timer->setSingleShot(false);
    m_timer->stop();
    //绑定回调函数
    connect(m_timer, &QTimer::timeout, this, &SerialWorker::do_timer_timeout);
}

SerialWorker::~SerialWorker()
{
    close();                                //手动关闭串口
    delete protocol;                        //手动析构
}

void SerialWorker::open(SerialConfig config)
{
    QList<QSerialPortInfo> com_list = QSerialPortInfo::availablePorts();
    QSerialPortInfo port_info = com_list.at(config.port);
    comPort->setPort(port_info);                     //设置端口
    //设置串口通信参数
    comPort->setBaudRate(config.baud);
    comPort->setDataBits(QSerialPort::DataBits(config.data));
    comPort->setStopBits(QSerialPort::StopBits(config.stop));
    comPort->setParity(QSerialPort::Parity(config.check));
    //打开串口
    bool res = comPort->open(QIODeviceBase::ReadWrite);
    if (res) {
        connect(comPort, &QSerialPort::readyRead, this, &SerialWorker::do_readyRead);
        emit serialOpened(true, "串口打开成功");
    }
    else    emit serialOpened(false, "串口打开失败");
}

void SerialWorker::close()
{
    if(comPort->isOpen())
    {
        //断开连接
        disconnect(comPort, &QSerialPort::readyRead, this, &SerialWorker::do_readyRead);
        comPort->close();
        protocol->init();                       //清空环形缓冲区，避免旧数据影响
        emit serialClose();
    }
}

void SerialWorker::start()                                            //启动定时器
{
    m_timer->start();
}

void SerialWorker::stop()                                             //停止定时器
{
    m_timer->stop();
}                                          

void SerialWorker::do_readyRead()
{   //进入数据读取循环
    uint8_t type;
    QByteArray rawData = comPort->readAll();   //获取串口缓冲区数据
    QByteArray data;                           //处理完后的数据

    protocol->receive(rawData);                //将接收的数据放入环形缓冲区
    //循环解析数据帧
    while(1)
    {
        if(protocol->process(type, data))     //接收到完整的数据帧
        {
            emit serialDataUpdated(type, data); //利用了Qt的隐式共享机制，减少后续拷贝的花销
        }
        else break;                            //没有完整帧，退出循环
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
    emit serialPortNumChanged(m_lastPorts);     //发送端口信息给UI主线程    
}

void SerialWorker::send(uint8_t type, const QByteArray& data)    //发送数据接口
{
    if(comPort->isOpen())
    {
        protocol->package(type, data);                          //将数据打包
        comPort->write(protocol->getRawData());                 //发送打包完后的原始数据
    }
}

