#include "data_processor.h"

DataProcessor::DataProcessor(QObject *parent)
{
    m_running = false;
    memset(&m_config, 0, sizeof(m_config));
}

DataProcessor::~DataProcessor()
{
    m_running = false;
    serial_close();  
}

void DataProcessor::setSerialPortConfig(serial_config_t *config)
{
    if(config == NULL) return;

    QMutexLocker locker(&m_mutex);  //使用互斥锁保护
    m_config.stop = config->stop;
    m_config.baud = config->baud;
    m_config.check = config->check;
    m_config.data = config->data;
    m_config.port = config->port;
}

void DataProcessor::do_sendData(uint8_t type, QByteArray data)
{
    uint8_t *buf;
    uint16_t len = data.size();
    protocol_package(type, (uint8_t *)data.constData(), len);  //数据打包
    buf = protocol_datas(&len);         //获取打包后的数据
    serial_send(buf, len);              //利用串口发送数据到下位机
}

void DataProcessor::openSerial()
{
    if(m_running)   return;
    serial_config_t config;             //读数据时也加互斥锁锁
    {
        QMutexLocker locker(&m_mutex);
        config = m_config;
    }
    //在data线程中打开串口
    serial_init(&config);
    if(!serial_open(0))                     //以读写方式打开串口  
    {
        emit serialStatusChanged(2);        //打开串口失败
        m_running = false;
        return;
    }
    emit serialStatusChanged(0);            //打开串口成功
    protocol_init(0);                       //协议对象初始化， 0：不显示数据

    //连接readyRead信号到本对象的槽函数
    connect(getSerial(), &QSerialPort::readyRead, this, &DataProcessor::do_readyRead);
    m_running = true;
}

void DataProcessor::stop()
{
    if(!m_running) return;
    m_running = false;                      

    //断开信号，避免被关闭后误触发
    disconnect(getSerial(), &QSerialPort::readyRead, this, &DataProcessor::do_readyRead);
    serial_close();
    emit serialStatusChanged(1);
}

void DataProcessor::do_readyRead()
{
    uint8_t datas[64];              //因为信号的发送是异步的，给主线程发送数据时，得发送值的拷贝，否则有可能数据会被覆盖
    //进入数据读取循环
    uint8_t* buf = NULL;
    uint16_t len = 0;
    uint8_t type;

    buf = serial_recv(&len);        //获取串口数据，阻塞延时。
    if(buf == NULL)     return;     //未接收到数据
    protocol_recv(buf, len);        //将接收的数据放入环形缓冲区
    //循环解析数据帧
    while(1)
    {
        if(protocol_process(&type, datas, &len))    //接收到完整的数据帧
        {
            //创建QByteArray副本传递数据，绝对安全
            emit dataReceived(type, QByteArray((char *)datas, len));    //发送数据给主线程
        }
        else break;                                 //没有完整帧，退出循环
    }
}