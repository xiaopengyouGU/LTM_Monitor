#include "status_bar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QTimer>
#include <QDateTime>

// ============================================================
// 私有实现（Pimpl）：标签/布局/时间定时器全部收敛于此
// ============================================================
class StatusBar::Private
{
public:
    explicit Private(StatusBar *status) : status(status) {}

    void build();                       // 创建标签与布局
    void setInfo(const QString &info);  // 信息显示
    void updateDateTime();              // 时间刷新
    void addSeparator(QBoxLayout *layout);

    StatusBar *status = nullptr;
    QLabel    *labWebside = nullptr;
    QLabel    *labWelcome = nullptr;
    QLabel    *labInfo = nullptr;
    QLabel    *labTime = nullptr;
    QTimer    *timer = nullptr;
};

void StatusBar::Private::build()
{
    QHBoxLayout *layout = new QHBoxLayout(status);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(15);

    // 网址标签
    labWebside = new QLabel(status);
    labWebside->setMinimumWidth(180);
    labWebside->setText("  www.ltm-monitor.com ");
    layout->addWidget(labWebside, 1);
    addSeparator(layout);

    // 欢迎标签
    labWelcome = new QLabel(status);
    labWelcome->setMinimumWidth(240);
    labWelcome->setText(" 欢迎使用：LTM-Monitor V0.4.3 ");
    layout->addWidget(labWelcome, 1);
    addSeparator(layout);

    // 信息标签
    labInfo = new QLabel(status);
    labInfo->setMinimumWidth(200);
    labInfo->setText(" 作者主页：https://gitee.com/xiaopengyouGU ");
    layout->addWidget(labInfo, 3);
    addSeparator(layout);

    // 时间标签
    labTime = new QLabel(status);
    labTime->setMinimumWidth(100);
    layout->addWidget(labTime);

    // 每秒刷新时间
    timer = new QTimer(status);
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, status, [this]() { updateDateTime(); });
    timer->start();
    updateDateTime();
}

void StatusBar::Private::setInfo(const QString &info)
{
    labInfo->setText(QString("状态：") + info);
}

void StatusBar::Private::updateDateTime()
{
    labTime->setText(QDateTime::currentDateTime().toString("yyyy/MM/dd  hh:mm:ss"));
}

void StatusBar::Private::addSeparator(QBoxLayout *layout)
{
    QFrame *line = new QFrame(status);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
}

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent) , pimpl(new Private(this))
{
    pimpl->build();
}

StatusBar::~StatusBar()
{
    delete pimpl;
}

void StatusBar::setInfo(const QString &info)
{
    pimpl->setInfo(info);
}
