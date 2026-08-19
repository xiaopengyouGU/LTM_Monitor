#include "channel_list_widget.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

namespace
{
    const char *kNameEdit  = "nameEdit";    // 行内控件 objectName（findChild 用）
    const char *kColorBtn  = "colorBtn";
    const char *kValueLbl  = "valueLabel";
    const char *kVisibleChk = "visibleChk";

// 数值最多 6 位小数，自动抹零
static QString fmtValue(double v)
{
    QString s = QString::number(v, 'f', 6);
    while (s.contains('.') && s.endsWith('0'))
        s.chop(1);
    if (s.endsWith('.'))
        s.chop(1);
    return s;
}
}

// ============================================================
// 私有实现（Pimpl）：通道号 -> 行映射收敛于此
// ============================================================
class ChannelListWidget::Private
{
public:
    explicit Private(ChannelListWidget *w) : w(w) {}

    QListWidgetItem *itemOf(int channel) const;
    void addChannel(int channel, const QString &name, const QColor &color, bool visible);
    void removeChannel(int channel);
    void clearChannels();
    bool hasChannel(int channel) const;
    void setChannelName(int channel, const QString &name);
    void setChannelColor(int channel, const QColor &color);
    void setChannelValue(int channel, double value, bool valid);
    void setChannelVisible(int channel, bool visible);

    ChannelListWidget *w = nullptr;
    QHash<int, QListWidgetItem*> m_items;   // 通道号 -> 行
};

QListWidgetItem *ChannelListWidget::Private::itemOf(int channel) const
{
    return m_items.value(channel, nullptr);
}

void ChannelListWidget::Private::addChannel(int channel, const QString &name,
                                            const QColor &color, bool visible)
{
    if (m_items.contains(channel))  return;

    QListWidgetItem *item = new QListWidgetItem(w);
    item->setSizeHint(QSize(0, 25));
    m_items.insert(channel, item);

    QWidget *row = new QWidget(w);
    QHBoxLayout *lay = new QHBoxLayout(row);
    lay->setContentsMargins(8, 2, 8, 2);
    lay->setSpacing(10);

    // 曲线颜色：点击弹色板
    QToolButton *colorBtn = new QToolButton(row);
    colorBtn->setObjectName(kColorBtn);
    colorBtn->setFixedSize(24, 22);
    colorBtn->setAutoFillBackground(true);
    colorBtn->setToolTip(QString("点击修改曲线颜色"));
    colorBtn->setProperty("color", color);
    colorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid gray; border-radius: 3px;").arg(color.name()));
    connect(colorBtn, &QToolButton::clicked, w, [this, channel, colorBtn]() {
        const QColor cur = colorBtn->property("color").value<QColor>();
        const QColor c = QColorDialog::getColor(cur.isValid() ? cur : Qt::white, w, QString("选择曲线颜色"));
        if (!c.isValid())  return;
        colorBtn->setProperty("color", c);
        colorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid gray; border-radius: 3px;").arg(c.name()));
        emit w->colorPicked(channel, c);
    });

    // 通道名
    QLineEdit *nameEdit = new QLineEdit(row);
    nameEdit->setObjectName(kNameEdit);
    nameEdit->setFrame(false);
    connect(nameEdit, &QLineEdit::textChanged, w, [this, channel](const QString &text) {
        emit w->nameEdited(channel, text);
    });

    // 实际值：中转站节流广播，valid=false 显示 "-"
    QLabel *valueLabel = new QLabel(QString("-"), row);
    valueLabel->setObjectName(kValueLbl);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setMinimumWidth(90);

    // 可见性
    QCheckBox *visibleChk = new QCheckBox(QString("可见"), row);
    visibleChk->setObjectName(kVisibleChk);
    visibleChk->setChecked(visible);
    connect(visibleChk, &QCheckBox::toggled, w, [this, channel](bool on) {
        emit w->visibleToggled(channel, on);
    });

    lay->addWidget(colorBtn);
    lay->addWidget(nameEdit, 1);
    lay->addWidget(valueLabel);
    lay->addWidget(visibleChk);
    w->setItemWidget(item, row);

    // 初始值
    setChannelName(channel, name);
    setChannelColor(channel, color);
}

void ChannelListWidget::Private::removeChannel(int channel)
{
    QListWidgetItem *item = m_items.take(channel);
    if (!item)  return;
    w->takeItem(w->row(item));
    delete item;
}

void ChannelListWidget::Private::clearChannels()
{
    m_items.clear();
    w->clear();
}

bool ChannelListWidget::Private::hasChannel(int channel) const
{
    return m_items.contains(channel);
}

void ChannelListWidget::Private::setChannelName(int channel, const QString &name)
{
    QListWidgetItem *item = itemOf(channel);
    QLineEdit *edit = item ? w->itemWidget(item)->findChild<QLineEdit *>(kNameEdit) : nullptr;
    if (!edit)  return;
    edit->blockSignals(true);
    edit->setText(name);
    edit->blockSignals(false);
}

void ChannelListWidget::Private::setChannelColor(int channel, const QColor &color)
{
    QListWidgetItem *item = itemOf(channel);
    QToolButton *btn = item ? w->itemWidget(item)->findChild<QToolButton *>(kColorBtn) : nullptr;
    if (!btn)  return;
    btn->setProperty("color", color);
    btn->setStyleSheet(QString("background-color: %1; border: 1px solid gray; border-radius: 3px;").arg(color.name()));
}

void ChannelListWidget::Private::setChannelValue(int channel, double value, bool valid)
{
    QListWidgetItem *item = itemOf(channel);
    QLabel *lbl = item ? w->itemWidget(item)->findChild<QLabel *>(kValueLbl) : nullptr;
    if (!lbl)  return;
    lbl->setText(valid ? fmtValue(value) : QString("-"));
}

void ChannelListWidget::Private::setChannelVisible(int channel, bool visible)
{
    QListWidgetItem *item = itemOf(channel);
    QCheckBox *chk = item ? w->itemWidget(item)->findChild<QCheckBox *>(kVisibleChk) : nullptr;
    if (!chk)  return;
    chk->blockSignals(true);
    chk->setChecked(visible);
    chk->blockSignals(false);
}

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
ChannelListWidget::ChannelListWidget(QWidget *parent)
    : QListWidget(parent)
    , pimpl(new Private(this))
{
}

ChannelListWidget::~ChannelListWidget()
{
    delete pimpl;
}

void ChannelListWidget::addChannel(int channel, const QString &name, const QColor &color, bool visible)
{
    pimpl->addChannel(channel, name, color, visible);
}

void ChannelListWidget::removeChannel(int channel)      { pimpl->removeChannel(channel); }
void ChannelListWidget::clearChannels()                 { pimpl->clearChannels(); }
bool ChannelListWidget::hasChannel(int channel) const   { return pimpl->hasChannel(channel); }

void ChannelListWidget::setChannelName(int channel, const QString &name)  { pimpl->setChannelName(channel, name); }
void ChannelListWidget::setChannelColor(int channel, const QColor &color) { pimpl->setChannelColor(channel, color); }
void ChannelListWidget::setChannelValue(int channel, double value, bool valid) { pimpl->setChannelValue(channel, value, valid); }
void ChannelListWidget::setChannelVisible(int channel, bool visible)      { pimpl->setChannelVisible(channel, visible); }
