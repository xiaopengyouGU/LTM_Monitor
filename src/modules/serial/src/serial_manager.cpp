
#include "serial_manager.h"
#include "serial_manager_private.h"
#include "serial_protocol.h"
#include "serial_worker.h"
#include <QThread>


SerialManager::Private::Private(SerialManager * parent):QObject(parent), m_manager(parent)
{  
    m_worker = new SerialWorker;
    data_thread = new QThread(this);
    m_worker->moveToThread(data_thread);
    
    //绑定信号与槽
    connect(m_worker, &SerialWorker::serialDataUpdated, this, &Private::do_serialDataUpdated);
    connect(m_worker, &SerialWorker::serialPortNumChanged, this, &Private::do_serialPortNumChanged);
    connect(m_worker, &SerialWorker::serialOpened, this, &Private::do_serialOpened);
    connect(m_worker, &SerialWorker::serialClose, this, &Private::do_serialClose);
    //
    connect(this, &Private::openSerial,  m_worker, &SerialWorker::open);
    connect(this, &Private::closeSerial, m_worker, &SerialWorker::close);
    connect(this, &Private::sendData,    m_worker, &SerialWorker::send);
    //线程启动后，再启动worker的定时器
    connect(data_thread, &QThread::started, m_worker, &SerialWorker::start);
}

SerialManager::Private::~Private()
{
    stop();
    delete m_worker;
}

void SerialManager::Private::start()
{
    data_thread->start();               
}

void SerialManager::Private::stop()
{   //避免出现定时器被UI线程关闭的情况
    QMetaObject::invokeMethod(m_worker, &SerialWorker::stop, Qt::BlockingQueuedConnection);
    // 停止所有线程的事件循环(关闭内部定时器等)
    data_thread->quit();
    data_thread->wait();
}

void SerialManager::Private::open(SerialConfig config)
{
    emit openSerial(config);
}

void SerialManager::Private::close()
{
    emit closeSerial();
}

void SerialManager::Private::send(uint8_t type, const QByteArray& data)
{
    emit sendData(type, data);
}

//信号中转
void SerialManager::Private::do_serialDataUpdated(uint8_t data_type, const QByteArray& data)  //数据更新
{
    emit m_manager->serialDataUpdated(data_type, data);
}

void SerialManager::Private::do_serialPortNumChanged(const QStringList& portNum)
{
    emit m_manager->serialPortNumChanged(portNum);
}

void SerialManager::Private::do_serialOpened(bool success, const QString& msg) //串口打开信号
{
    emit m_manager->serialOpened(success, msg);
}

void SerialManager::Private::do_serialClose()                                  //串口关闭信号
{
    emit m_manager->serialClose();
} 

// ========== SerailManager 公共接口实现 ==========
SerialManager::SerialManager(QObject *parent) : QObject(parent), 
    pimpl(new Private(this)){}
SerialManager::~SerialManager(){};                //Qt负责内存管理

void SerialManager::start()                      { pimpl->start(); }
void SerialManager::stop()                       { pimpl->stop(); }
void SerialManager::open(SerialConfig config)    { pimpl->open(config); }
void SerialManager::close()                      { pimpl->close(); }
void SerialManager::send(uint8_t type, const QByteArray& data)    { pimpl->send(type, data);}