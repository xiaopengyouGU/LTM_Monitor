#include "pid_widget.h"
#include "ui_pid_widget.h"

#include "data_hub.h"            
#include "ltm_protocol.h"
#include "serial.h"
#include "status_bar.h"
#include <cstring>

#define PID_CHANNEL_SIZE    5    // PID 通道数（与 LTM 曲线通道数一致）

// 动态显示小数位：按值大小选小数位数（>10000 不显；>1000 一位；>30 两位；否则三位），再去尾零/去尾点
static QString data2Str(float value)
{
    int pos = 3;
    const float abs_val = qAbs(value);
    if (abs_val > 10000)      pos = 0;
    else if (abs_val > 1000)  pos = 1;
    else if (abs_val > 30)    pos = 2;
    QString s = QString::number(value, 'f', pos);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.')))
        s.chop(1);
    return s;
}

// PID 通道数据（仅本文件可见）
struct PidData {
    float Kp;
    float Ki;
    float Kd;
    float dt;       // 周期值
    float target;   // 目标值
    float actual;   // 实际值
};

// ============================================================
// 私有实现（Pimpl）：通道数组与全部逻辑收敛于此
// ============================================================
class PidWidget::Private
{
public:
    explicit Private(PidWidget *q) : q(q) {}

    void setup();                   // UI 创建 + 通道数据初始化
    void setValue(int ch, float value);
    void refreshDisplay();          // 回显当前通道 PID 数据
    void refreshActual();           // 只刷新实际值（动态小数位）
    void setInfo(const QString &msg);

    void onComboChChanged(int index);
    void onTarget();
    void onPID();
    void onPeriod();

    PidWidget *q = nullptr;
    Ui::PidWidget *ui = nullptr;
    QList<PidData> m_pidData;       // PID 通道数据（内部持有）
    SerialManager *m_manager = nullptr;
    DataHub       *m_hub     = nullptr;      // 发送路由（可空）
    StatusBar     *m_bar     = nullptr;
};

void PidWidget::Private::setup()
{
    ui = new Ui::PidWidget;
    ui->setupUi(q);

    m_pidData.resize(PID_CHANNEL_SIZE);
    for (int i = 0; i < PID_CHANNEL_SIZE; i++)
        memset(&m_pidData[i], 0, sizeof(PidData));
}

void PidWidget::Private::setValue(int ch, float value)
{
    if (ch < 0 || ch >= m_pidData.size()) return;   // 越界忽略
    m_pidData[ch].actual = value;                   // 强写实际值（DataHub 直调）
}

void PidWidget::Private::setInfo(const QString &msg)
{
    if (m_bar) m_bar->setInfo(msg);
}

void PidWidget::Private::refreshDisplay()
{
    const int ch = ui->comboCh->currentIndex();
    if (ch < 0 || ch >= m_pidData.size() || ch >= MAX_CHANNEL_SIZE) return;
    const PidData &pid = m_pidData[ch];
    ui->editP->setText(QString("%1").arg(pid.Kp));
    ui->editI->setText(QString("%1").arg(pid.Ki));
    ui->editD->setText(QString("%1").arg(pid.Kd));
    ui->editTarget->setText(QString("%1").arg(pid.target));
    ui->editActual->setText(data2Str(pid.actual));          // 动态小数位
    ui->spinPeriod->setValue(pid.dt);
}

void PidWidget::Private::refreshActual()
{
    const int ch = ui->comboCh->currentIndex();
    if (ch < 0 || ch >= m_pidData.size() || ch >= MAX_CHANNEL_SIZE) return;
    ui->editActual->setText(data2Str(m_pidData[ch].actual));   // 只动实际值，不碰目标值/参数
}

void PidWidget::Private::onComboChChanged(int index)
{
    if (index >= MAX_CHANNEL_SIZE) return;
    refreshDisplay();
    setInfo("控制通道切换");
}

void PidWidget::Private::onTarget()
{
    if (!m_manager) return;
    const int ch = ui->comboCh->currentIndex();
    if (ch >= m_pidData.size() || ch >= MAX_CHANNEL_SIZE) return;
    const float value = ui->editTarget->text().toFloat();
    m_pidData[ch].target = value;
    QByteArray data((const char *)&value, sizeof(value));
    if (m_hub) m_hub->sendLtm(Data_Target, data);
}

void PidWidget::Private::onPID()
{
    if (!m_manager) return;
    const int ch = ui->comboCh->currentIndex();
    if (ch >= m_pidData.size() || ch >= MAX_CHANNEL_SIZE) return;

    float pid[3];
    pid[0] = ui->editP->text().toFloat();
    pid[1] = ui->editI->text().toFloat();
    pid[2] = ui->editD->text().toFloat();
    m_pidData[ch].Kp = pid[0];
    m_pidData[ch].Ki = pid[1];
    m_pidData[ch].Kd = pid[2];
    QByteArray data((const char *)&pid, sizeof(pid));
    if (m_hub) m_hub->sendLtm(Data_CMD_Set_PID, data);
    setInfo("成功发送PID参数");
}

void PidWidget::Private::onPeriod()
{
    if (!m_manager) return;
    const int ch = ui->comboCh->currentIndex();
    if (ch >= m_pidData.size() || ch >= MAX_CHANNEL_SIZE) return;
    const float period = (float)ui->spinPeriod->value();
    m_pidData[ch].dt = period;
    QByteArray data((const char *)&period, sizeof(period));
    if (m_hub) m_hub->sendLtm(Data_CMD_Set_Period, data);
    setInfo("设置PID周期(ms)");
}

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
PidWidget::PidWidget(QWidget *parent) : QWidget(parent), pimpl(new Private(this)){ pimpl->setup(); }
PidWidget::~PidWidget()                       { delete pimpl; }
void PidWidget::connectManager(SerialManager *manager) { if (manager) pimpl->m_manager = manager; }
void PidWidget::connectHub(DataHub *hub)      { if (hub)  pimpl->m_hub = hub; }
void PidWidget::setStatusBar(StatusBar *bar)  { pimpl->m_bar = bar; }
void PidWidget::setValue(int ch, float value) { pimpl->setValue(ch, value); }
void PidWidget::refreshDisplay()              { pimpl->refreshDisplay(); }
void PidWidget::refreshActual()               { pimpl->refreshActual(); }
void PidWidget::on_comboCh_currentIndexChanged(int index) { pimpl->onComboChChanged(index); }
void PidWidget::on_btnTarget_clicked()        { pimpl->onTarget(); }
void PidWidget::on_btnPID_clicked()           { pimpl->onPID(); }
void PidWidget::on_btnPeriod_clicked()        { pimpl->onPeriod(); }
