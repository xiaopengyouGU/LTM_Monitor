#include "serial.h"

static serial_obj _serial_obj;
static serial_obj *serial = &_serial_obj;

void serial_init(serial_config_t *config)
{
    QList<QSerialPortInfo> com_list = QSerialPortInfo::availablePorts();
    QSerialPortInfo port_info = com_list.at(config->port);
    serial->comPort.setPort(port_info);                     //设置端口
    //设置串口通信参数
    serial->comPort.setBaudRate(config->baud);
    serial->comPort.setDataBits(QSerialPort::DataBits(config->data));
    serial->comPort.setStopBits(QSerialPort::StopBits(config->stop));
    serial->comPort.setParity(QSerialPort::Parity(config->check));
}

void serial_send(uint8_t* buf, uint16_t len)
{
    if(buf == NULL || len == 0) return;

    if(serial->comPort.isOpen())
    {
        serial->comPort.write((const char*)buf, len);
    }
}

uint8_t* serial_recv(uint16_t *len)
{
    QByteArray data = serial->comPort.readAll();
    *len = data.size();
    if(*len == 0) return NULL;
    memcpy(serial->buf,(uint8_t*)data.constData(), *len);
    
    return serial->buf;
}

void serial_close(void)
{
    if(serial->comPort.isOpen())
        serial->comPort.close();
}       

bool serial_open(uint8_t flag)
{
    if(flag == 0)
        return serial->comPort.open(QIODeviceBase::ReadWrite);
    else if(flag == 1)
        return serial->comPort.open(QIODeviceBase::ReadOnly);
    else if(flag == 2)
        return serial->comPort.open(QIODeviceBase::WriteOnly);
    else return false;
}           

//为了避免Qt的线程亲缘性问题（对QObject等），需要提前将QSerialPort对象移动到data线程中
void moveSerialToThread(QThread *thread)
{
    if(thread != nullptr && &serial->comPort != nullptr) 
    {
        serial->comPort.moveToThread(thread);
    }
}

QSerialPort* getSerial(void)
{
    return &(serial->comPort);
}