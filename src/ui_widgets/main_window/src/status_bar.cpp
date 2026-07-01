#include "status_bar.h"
#include <QHBoxLayout>
#include <QDateTime>
#include <QFrame>

StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent)
{
    // 创建水平布局
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(15);

    // 作者标签
    labAuthor = new QLabel(this);
    labAuthor->setMinimumWidth(100);
    labAuthor->setText("  Lvtou (宁波)");
    layout->addWidget(labAuthor);
    addSeparator(layout);

    // 网址标签
    labWebside = new QLabel(this);
    labWebside->setMinimumWidth(350);
    labWebside->setText(" https://gitee.com/xiaopengyouGU/LTM_Monitor");
    layout->addWidget(labWebside, 1);
    addSeparator(layout);

    // 信息标签
    labInfo = new QLabel(this);
    labInfo->setMinimumWidth(300);
    labInfo->setText("欢迎使用: LTM_Monitor V0.2.0");
    layout->addWidget(labInfo, 3);
    addSeparator(layout);

    // 时间标签
    labTime = new QLabel(this);
    labTime->setMinimumWidth(100);
    layout->addWidget(labTime);

    setLayout(layout);

    // 创建定时器，每秒更新时间
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &StatusBar::updateDateTime);
    m_timer->start();

    // 初始显示时间
    updateDateTime();
}

StatusBar::~StatusBar()
{
    if (m_timer) {
        m_timer->stop();
    }
}

void StatusBar::setInfo(const QString &info)   //设置信息栏显示
{
    QString str = QString("状态：") + info;
    labInfo->setText(str);
}

void StatusBar::updateDateTime()
{
    QDateTime now = QDateTime::currentDateTime();
    QString str = now.toString("yyyy/MM/dd  hh:mm:ss");
    labTime->setText(str);
}

void StatusBar::addSeparator(QBoxLayout *layout)
{
    QFrame *line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
}
