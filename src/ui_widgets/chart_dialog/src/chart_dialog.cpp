#include "chart_dialog.h"
#include "channel_list_widget.h"
#include "chart_manager.h"
#include "ui_chart_dialog.h"

#include <cmath>

// 默认曲线配色：与模块 chart_def.h 目标/实际色一致，循环分配
static const QList<QColor> kDefaultColors = {
    Qt::red, QColor(255, 128, 0), Qt::magenta, Qt::blue, Qt::darkBlue,
    Qt::green, Qt::yellow, Qt::darkGreen, Qt::cyan, Qt::darkYellow,
    QColor(128, 0, 128), QColor(255, 192, 203), QColor(139, 69, 19),
    QColor(128, 128, 0), QColor(75, 0, 130), QColor(144, 238, 144),
    QColor(255, 165, 0), QColor(138, 43, 226), QColor(135, 206, 235),
    QColor(255, 127, 80)
};

// ============================================================
// 私有实现（Pimpl）：UI 与通道状态收敛于此
// ============================================================
class ChartDialog::Private
{
public:
    explicit Private(ChartDialog *dlg) : dlg(dlg) {}

    void setup();                       // UI 创建 + 通道列表信号接线
    void connectManager(ChartManager *manager, int channelCount);
    void onChannelValues(const QList<double> &values);
    void onChannelName(int channel, const QString &name);
    void onChannelColor(int channel, const QColor &color);
    void onChannelVisible(int channel, bool visible);
    void onViewChanged(int index);
    void onTimeChanged(int index);
    void onColorChanged(int index);
    void onRangeChanged(int index);
    void onClearShow();
    void onStopShow();

    ChartDialog     *dlg = nullptr;
    Ui::ChartDialog *ui = nullptr;
    ChartManager    *m_manager = nullptr;
    int             m_channelCount = 0;
};

void ChartDialog::Private::setup()
{
    ui = new Ui::ChartDialog;
    ui->setupUi(dlg);

    // 通道列表 -> 图表管理器（lambda 以 dlg 为接收上下文，随控件销毁自动断开）
    connect(ui->channelList, &ChannelListWidget::nameEdited, dlg,
            [this](int channel, const QString &name) { onChannelName(channel, name); });
    connect(ui->channelList, &ChannelListWidget::colorPicked, dlg,
            [this](int channel, const QColor &color) { onChannelColor(channel, color); });
    connect(ui->channelList, &ChannelListWidget::visibleToggled, dlg,
            [this](int channel, bool visible) { onChannelVisible(channel, visible); });
}

void ChartDialog::Private::connectManager(ChartManager *manager, int channelCount)
{
    if (!manager) return;
    m_manager = manager;
    m_channelCount = channelCount;

    // 动态生成通道行：颜色 + 通道名 + 实际值（"-"占位）+ 可见性
    ui->channelList->clearChannels();
    for (int ch = 0; ch < m_channelCount; ++ch) {
        const QColor color = kDefaultColors[ch % kDefaultColors.size()];
        const bool visible = (ch < 2);                      // 默认只显示 CH0-CH1
        m_manager->setChannelColor(ch, color);              // 曲线颜色与列表一致
        m_manager->setChannelVisible(ch, visible);          // 默认可见性与曲线同步
        ui->channelList->addChannel(ch, QString("CH%1").arg(ch), color, visible);
    }
}

void ChartDialog::Private::onChannelValues(const QList<double> &values)
{   // 中转站节流广播：刷新各通道实际值（NaN = 无数据）
    const int n = qMin(m_channelCount, values.size());
    for (int ch = 0; ch < n; ++ch)
        ui->channelList->setChannelValue(ch, values[ch], !std::isnan(values[ch]));
}

void ChartDialog::Private::onChannelName(int channel, const QString &name)
{
    if (!m_manager) return;
    m_manager->setChannelName(channel, name);
}

void ChartDialog::Private::onChannelColor(int channel, const QColor &color)
{
    if (!m_manager) return;
    m_manager->setChannelColor(channel, color);
}

void ChartDialog::Private::onChannelVisible(int channel, bool visible)
{
    if (!m_manager) return;
    m_manager->setChannelVisible(channel, visible);
}

void ChartDialog::Private::onViewChanged(int index)
{
    if (index < 0) return;
    emit dlg->viewChanged(index);                           // MainWindow 切换当前视图
    if (m_manager) {                                        // 时间/背景设置作用于当前视图
        m_manager->setAbsTime(index, ui->comboTime->currentIndex() != 0);
        m_manager->setBackColor(index, ui->comboColor->currentIndex());
    }
}

void ChartDialog::Private::onTimeChanged(int index)
{
    if (!m_manager) return;
    m_manager->setAbsTime(ui->comboView->currentIndex(), index != 0);
}

void ChartDialog::Private::onColorChanged(int index)
{
    if (!m_manager) return;
    m_manager->setBackColor(ui->comboView->currentIndex(), index);
}

void ChartDialog::Private::onRangeChanged(int index)
{
    if (!m_manager) return;
    static const double kRangeSeconds[] = { 5.0, 10.0, 30.0, 60.0, 120.0, 300.0, 900.0, 1800.0 };
    if (index < 0 || index >= int(sizeof(kRangeSeconds) / sizeof(kRangeSeconds[0]))) return;
    m_manager->setWindowLen(kRangeSeconds[index]);
}

void ChartDialog::Private::onClearShow()
{
    if (!m_manager) return;
    m_manager->clearShow();
}

void ChartDialog::Private::onStopShow()
{
    if (!m_manager) return;
    m_manager->stopShow();
    for (int ch = 0; ch < m_channelCount; ++ch)
        ui->channelList->setChannelVisible(ch, false);
}

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
ChartDialog::ChartDialog(QWidget *parent)
    : QDialog(parent)
    , pimpl(new Private(this))
{
    pimpl->setup();
}

ChartDialog::~ChartDialog()
{
    delete pimpl;
}

void ChartDialog::connectManager(ChartManager *manager, int channelCount)
{
    pimpl->connectManager(manager, channelCount);
}

void ChartDialog::do_channelValues(const QList<double> &values)
{
    pimpl->onChannelValues(values);
}

void ChartDialog::on_comboView_currentIndexChanged(int index)    { pimpl->onViewChanged(index); }
void ChartDialog::on_comboTime_currentIndexChanged(int index)    { pimpl->onTimeChanged(index); }
void ChartDialog::on_comboColor_currentIndexChanged(int index)   { pimpl->onColorChanged(index); }
void ChartDialog::on_comboRange_currentIndexChanged(int index)   { pimpl->onRangeChanged(index); }
void ChartDialog::on_btnClearShow_clicked()                      { pimpl->onClearShow(); }
void ChartDialog::on_btnStopShow_clicked()                       { pimpl->onStopShow(); }
