#include "modbus_master.h"

#include <QTimer>
#include <deque>

class ModbusMaster::Private : public QObject
{
    Q_OBJECT
public:
    explicit Private(ModbusMaster *master);

    void setSendCallback(std::function<void(const QByteArray &)> cb) { m_send = cb; }
    void receive(const QByteArray &bytes);
    void start(const ModbusConfig &cfg);
    void stop();
    void send(const ModbusConfig &cfg, const QByteArray &data);

signals:
    void modbusResponse(const ModbusConfig &cfg, const QByteArray &data);
    void modbusException(const ModbusConfig &cfg, uint8_t code);
    void modbusTimeout(int missCount);

private slots:
    void do_pollTimeout();
    void do_watchdogTimeout();

private:
    struct ModbusTx {
        ModbusConfig cfg;
        QByteArray   payload;
    };
    void enqueue(const ModbusConfig &cfg, const QByteArray &payload, bool front);
    void pump();                                    // 事务泵：idle 出队发送

    ModbusProtocol m_modbus;                        // 协议解析（components/protocol）
    std::function<void(const QByteArray &)> m_send; // 传输注入

    QTimer *m_pollTimer = nullptr;
    QTimer *m_watchdog  = nullptr;
    std::deque<ModbusTx> m_queue;
    ModbusTx     m_pending;
    ModbusConfig m_pollCfg;
    bool m_pollOn = false;
    bool m_busy   = false;
    int  m_miss   = 0;
};

ModbusMaster::Private::Private(ModbusMaster *master)
    : QObject(master)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    m_pollTimer->setSingleShot(false);
    connect(m_pollTimer, &QTimer::timeout, this, &Private::do_pollTimeout);

    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(300);
    connect(m_watchdog, &QTimer::timeout, this, &Private::do_watchdogTimeout);
}

void ModbusMaster::Private::start(const ModbusConfig &cfg)
{
    m_pollCfg = cfg;
    m_pollOn  = true;
    m_miss    = 0;
    m_pollTimer->start(cfg.intervalMs);
    QByteArray qty;                                 // 读请求数据区 = 数量（2 字节大端）
    qty.append((char)(cfg.quantity >> 8));
    qty.append((char)(cfg.quantity & 0xFF));
    enqueue(cfg, qty, false);
}

void ModbusMaster::Private::stop()
{
    m_pollOn = false;
    m_pollTimer->stop();
    m_watchdog->stop();
    m_queue.clear();
    m_busy = false;
    m_modbus.init();
}

void ModbusMaster::Private::send(const ModbusConfig &cfg, const QByteArray &data)
{
    enqueue(cfg, data, true);                       // 插队（配置由调用方直接给出）
}

void ModbusMaster::Private::receive(const QByteArray &bytes)
{
    if (!m_busy)
        return;                                     // 空闲不接字节，避免过期配置参与解析
    m_modbus.receive(bytes);
    QByteArray data;
    if (m_modbus.process(m_pending.cfg, data)) {    // 主站一个请求对应一个响应
        if (m_pending.cfg.func & 0x80)
            emit modbusException(m_pending.cfg, (uint8_t)data.at(0));
        else
            emit modbusResponse(m_pending.cfg, data);
        m_watchdog->stop();                         // 喂狗
        m_busy = false;                             // 释放总线
        pump();                                     // 尝试发下一个事务
    }
}

void ModbusMaster::Private::enqueue(const ModbusConfig &cfg, const QByteArray &payload, bool front)
{
    ModbusTx it;
    it.cfg     = cfg;
    it.payload = payload;
    if (front)  m_queue.push_front(it);
    else        m_queue.push_back(it);
    pump();
}

void ModbusMaster::Private::pump()
{
    if (m_busy || m_queue.empty())
        return;
    m_pending = m_queue.front();
    m_queue.pop_front();
    m_busy = true;
    m_watchdog->start();
    if (m_send)
        m_send(m_modbus.package(m_pending.cfg, m_pending.payload));
}

void ModbusMaster::Private::do_pollTimeout()
{
    if (!m_pollOn || m_busy || !m_queue.empty())
        return;                                     // 去重：在途或队列非空则跳过
    QByteArray qty;
    qty.append((char)(m_pollCfg.quantity >> 8));
    qty.append((char)(m_pollCfg.quantity & 0xFF));
    enqueue(m_pollCfg, qty, false);
}

void ModbusMaster::Private::do_watchdogTimeout()
{
    m_miss++;
    emit modbusTimeout(m_miss);
    m_busy = false;                                 // 总线强制释放
    pump();
}

/*==================== 公共 API（委托 Pimpl） ====================*/
ModbusMaster::ModbusMaster(QObject *parent) : QObject(parent) , pimpl(new Private(this))
{
    connect(pimpl, &Private::modbusResponse, this, &ModbusMaster::modbusResponse);
    connect(pimpl, &Private::modbusException, this, &ModbusMaster::modbusException);
    connect(pimpl, &Private::modbusTimeout,  this, &ModbusMaster::modbusTimeout);
}

ModbusMaster::~ModbusMaster()   { delete pimpl; }
void ModbusMaster::setSendCallback(std::function<void(const QByteArray &)> cb) { pimpl->setSendCallback(cb); }
void ModbusMaster::receive(const QByteArray &bytes) { pimpl->receive(bytes); }
void ModbusMaster::start(const ModbusConfig &cfg)   { pimpl->start(cfg); }
void ModbusMaster::stop()       { pimpl->stop(); }
void ModbusMaster::send(const ModbusConfig &cfg, const QByteArray &data) { pimpl->send(cfg, data); }

#include "modbus_master.moc"
