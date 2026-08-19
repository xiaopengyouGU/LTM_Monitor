#include "uds_server.h"

#include <QTimer>

// ============================================================
// UDS 升级常量（与 BootLoader 严格对齐）
// ============================================================
static constexpr quint8  UDS_SESSION_PROG = 0x02;     // 编程会话
static constexpr quint32 UDS_KEY          = 0x005A;   // 安全访问密钥（与 BootLoader 固定密钥一致）

// CRC-32 (IEEE 802.3)，与 BootLoader 端 _crc32_update 语义一致
static quint32 crc32_update(const quint8 *data, int len, quint32 state)
{
    for (int i = 0; i < len; i++) {
        state ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (state & 1U)
                state = (state >> 1) ^ 0xEDB88320U;
            else
                state >>= 1;
        }
    }
    return state;
}

// 大端 32 位追加：UDS 载荷（地址/长度）为大端序
static void append_be32(QByteArray &out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

// 大端 32 位读取：UDS 载荷（CRC 等）为大端序
static quint32 read_be32(const QByteArray &in, int off)
{
    return (quint32(quint8(in[off])) << 24) |
           (quint32(quint8(in[off + 1])) << 16) |
           (quint32(quint8(in[off + 2])) << 8)  |
            quint32(quint8(in[off + 3]));
}

// ============================================================
// 私有实现（Pimpl）：状态机与全部协议状态收敛于此，公共头不暴露
// ============================================================
class UdsServer::Private
{
public:
    explicit Private(UdsServer *uds) : uds(uds) {}

    // UDS 升级状态机
    enum State
    {
        Uds_Idle = 0,
        Uds_Session,            // 等待 0x50 02
        Uds_SecuritySeed,       // 等待 0x67 01 + seed
        Uds_SecurityKey,        // 等待 0x67 02
        Uds_RequestDownload,    // 等待 0x74
        Uds_Transfer,           // 等待 0x76
        Uds_TransferExit,       // 等待 0x77 + CRC
        Uds_Reset,              // 等待 0x51 01
        Uds_Done
    };

    void send(const QByteArray &payload);       // 发请求 + 启动超时
    void handleResponse(const QByteArray &resp);
    void nextTransferBlock();
    void finish(bool ok, const QString &msg);
    void onTimeout();

    UdsServer   *uds = nullptr;                 // 外层（发信号用）
    QTimer      *timer = nullptr;

    quint32     appBase   = 0x02008000;
    int         chunkSize = 60;
    int         rxTimeout = 1000;
    int         maxRetry  = 3;

    State       state     = Uds_Idle;
    QByteArray  firmware;
    quint8      seq       = 0;
    int         sendPos   = 0;                  // 已发送字节数
    quint32     crcLocal  = 0xFFFFFFFFUL;       // 本地计算 CRC（边发边算）
    int         retry     = 0;
    bool        stop      = false;
};

// ============================================================
// 公共接口：全部委托给私有实现
// ============================================================
UdsServer::UdsServer(QObject *parent)
    : QObject(parent)
    , pimpl(new Private(this))
{
    pimpl->timer = new QTimer(this);
    pimpl->timer->setSingleShot(true);
    connect(pimpl->timer, &QTimer::timeout, this, [this]() { pimpl->onTimeout(); });
}

UdsServer::~UdsServer()
{
    delete pimpl;
}

void UdsServer::setAppBase(quint32 addr) { pimpl->appBase   = addr; }
void UdsServer::setChunkSize(int bytes)  { pimpl->chunkSize = qMax(1, bytes); }
void UdsServer::setRxTimeout(int ms)     { pimpl->rxTimeout = qMax(10, ms); }
void UdsServer::setMaxRetry(int n)       { pimpl->maxRetry  = qMax(0, n); }

bool UdsServer::isActive() const
{
    return (pimpl->state != Private::Uds_Idle &&
            pimpl->state != Private::Uds_Done);
}

void UdsServer::start(const QByteArray &firmware)
{
    Private *d = pimpl;
    if (firmware.isEmpty()) {
        emit finished(false, "固件为空");
        return;
    }

    d->firmware = firmware;
    d->state    = Private::Uds_Session;
    d->seq      = 0;
    d->sendPos  = 0;
    d->retry    = 0;
    d->stop     = false;
    d->crcLocal = 0xFFFFFFFFUL;

    emit activeChanged(true);
    emit progressChanged(0, "编程会话...");

    QByteArray req;
    req.append(char(0x10));
    req.append(char(UDS_SESSION_PROG));
    d->send(req);
}

void UdsServer::stop()
{
    pimpl->stop = true;
    pimpl->timer->stop();
    pimpl->finish(false, "用户停止");
}

void UdsServer::onResponse(const QByteArray &resp)
{
    Private *d = pimpl;
    if (d->state == Private::Uds_Idle ||
        d->state == Private::Uds_Done) return;
    d->timer->stop();
    d->retry = 0;
    d->handleResponse(resp);
}

// ============================================================
// 私有实现细节
// ============================================================
void UdsServer::Private::nextTransferBlock()
{
    const int n = qMin(chunkSize, firmware.size() - sendPos);
    QByteArray req;
    req.append(char(0x36));
    req.append(char(++seq));
    req.append(firmware.mid(sendPos, n));

    // 边发边算 CRC（内部状态累计，与 BootLoader 一致）
    crcLocal = crc32_update(reinterpret_cast<const quint8 *>(firmware.constData() + sendPos), n, crcLocal);
    sendPos += n;

    emit uds->progressChanged(int(100.0 * sendPos / firmware.size()),
                              QString("传输数据 %1/%2").arg(sendPos).arg(firmware.size()));
    send(req);
}

void UdsServer::Private::handleResponse(const QByteArray &resp)
{
    if (resp.size() < 1) return;
    const quint8 sid = quint8(resp[0]);

    // 负响应：0x7F + SID + NRC
    if (sid == 0x7F && resp.size() >= 3) {
        finish(false, QString("NRC 0x%1 (SID 0x%2)")
                   .arg(quint8(resp[2]), 2, 16, QChar('0'))
                   .arg(quint8(resp[1]), 2, 16, QChar('0')));
        return;
    }

    switch (state) {
        case Uds_Session:                       // 0x50 02
            if (sid == 0x50) {
                state = Uds_SecuritySeed;
                emit uds->progressChanged(0, "安全访问(种子)...");
                QByteArray req; req.append(char(0x27)); req.append(char(0x01));
                send(req);
            } else { finish(false, "会话响应异常"); }
            break;

        case Uds_SecuritySeed:                  // 0x67 01 + seed（本实现不校验 seed）
            if (sid == 0x67) {
                state = Uds_SecurityKey;
                emit uds->progressChanged(0, "安全访问(密钥)...");
                QByteArray req;
                req.append(char(0x27));
                req.append(char(0x02));
                req.append(char((UDS_KEY >> 8) & 0xFF));    // 密钥 0x005A
                req.append(char(UDS_KEY & 0xFF));
                send(req);
            } else { finish(false, "种子响应异常"); }
            break;

        case Uds_SecurityKey:                   // 0x67 02
            if (sid == 0x67) {
                state = Uds_RequestDownload;
                emit uds->progressChanged(0, "请求下载...");
                QByteArray req;
                req.append(char(0x34));
                req.append(char(0x00));                     // DFI
                req.append(char(0x00));                     // ALFI：4B 地址 + 4B 长度
                append_be32(req, appBase);                  // 大端32位发送
                append_be32(req, quint32(firmware.size()));
                send(req);
            } else { finish(false, "密钥响应异常"); }
            break;

        case Uds_RequestDownload:               // 0x74
            if (sid == 0x74) {
                state = Uds_Transfer;
                emit uds->progressChanged(0, "传输数据...");
                nextTransferBlock();
            } else { finish(false, "下载请求响应异常"); }
            break;

        case Uds_Transfer:                      // 0x76
            if (sid == 0x76) {
                if (sendPos >= firmware.size()) {
                    state = Uds_TransferExit;
                    emit uds->progressChanged(100, "校验 CRC...");
                    QByteArray req; req.append(char(0x37));
                    send(req);
                } else {
                    nextTransferBlock();
                }
            } else { finish(false, "传输响应异常"); }
            break;

        case Uds_TransferExit:                  // 0x77 + CRC
            if (sid == 0x77 && resp.size() >= 5) {
                const quint32 devCrc = read_be32(resp, 1);  // 大端32位读取
                const quint32 localCrc = ~crcLocal;         // 取反得到标准 CRC-32
                if (devCrc == localCrc) {
                    state = Uds_Reset;
                    emit uds->progressChanged(100, "ECU 复位...");
                    QByteArray req; req.append(char(0x11)); req.append(char(0x01));
                    send(req);
                } else {
                    finish(false, QString("CRC 不匹配：设备 0x%1 本地 0x%2")
                            .arg(devCrc, 8, 16, QChar('0'))
                            .arg(localCrc, 8, 16, QChar('0')));
                }
            } else { finish(false, "退出传输响应异常"); }
            break;

        case Uds_Reset:                         // 0x51 01
            if (sid == 0x51) {
                state = Uds_Done;
                finish(true, "升级完成，设备已复位");
            } else { finish(false, "复位响应异常"); }
            break;

        default: break;
    }
}

// ============================================================
// 请求发送与超时重试
// ============================================================
void UdsServer::Private::send(const QByteArray &payload)
{
    if (stop) return;
    retry = 0;
    emit uds->sendRequest(payload);
    timer->start(rxTimeout);
}

void UdsServer::Private::onTimeout()
{
    if (stop || state == Uds_Idle || state == Uds_Done) return;
    if (++retry > maxRetry) {
        finish(false, "响应超时");
        return;
    }
    emit uds->logMessage(QString("响应超时，重试 %1/%2").arg(retry).arg(maxRetry));

    // 重发当前请求：按状态重发
    switch (state)
    {
        case Uds_Session:      { QByteArray r; r.append(char(0x10)); r.append(char(UDS_SESSION_PROG)); emit uds->sendRequest(r); break; }
        case Uds_SecuritySeed: { QByteArray r; r.append(char(0x27)); r.append(char(0x01)); emit uds->sendRequest(r); break; }
        case Uds_SecurityKey:  { QByteArray r; r.append(char(0x27)); r.append(char(0x02)); r.append(char((UDS_KEY >> 8) & 0xFF)); r.append(char(UDS_KEY & 0xFF)); emit uds->sendRequest(r); break; }
        case Uds_Transfer: nextTransferBlock(); return;       // 传输块不可重复序号，直接发下一块
        case Uds_TransferExit: { QByteArray r; r.append(char(0x37)); emit uds->sendRequest(r); break; }
        case Uds_Reset:        { QByteArray r; r.append(char(0x11)); r.append(char(0x01)); emit uds->sendRequest(r); break; }
        default: break;
    }
    timer->start(rxTimeout);
}

void UdsServer::Private::finish(bool ok, const QString &msg)
{
    timer->stop();
    state = Uds_Idle;
    emit uds->progressChanged(ok ? 100 : 0, ok ? "完成" : "失败");
    emit uds->logMessage(QString("%1 %2").arg(ok ? "[OK]" : "[FAIL]").arg(msg));
    emit uds->finished(ok, msg);
    emit uds->activeChanged(false);
}
