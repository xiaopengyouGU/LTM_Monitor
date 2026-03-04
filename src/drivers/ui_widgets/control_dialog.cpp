#include "control_dialog.h"
#include "ui_control_dialog.h"

ControlDialog::ControlDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ControlDialog)
{
    ui->setupUi(this);
    // 初始化可见性数组（默认只有通道1数据可见）
    for (int i = 0; i < 5; ++i) {
        m_targetVisible[i] = false;
        m_actualVisible[i] = false;
    }
    m_targetVisible[0] = true;
    m_actualVisible[0] = true;
    ui->chkTar1->setChecked(true);
    ui->chkAct1->setChecked(true);
    // 颜色数组（必须与 ChartManager 中定义的一致）
    const QColor targetColors[5] = {
        Qt::red, QColor(255, 128, 0), Qt::magenta, Qt::blue, Qt::darkBlue
    };
    const QColor actualColors[5] = {
        Qt::green, Qt::yellow, Qt::darkGreen, Qt::cyan, Qt::darkYellow
    };

    // 设置目标值复选框文本颜色
    for (int i = 0; i < 5; ++i)
    {
        QString tarName = QString("chkTar%1").arg(i+1);
        QCheckBox* chkTarget = findChild<QCheckBox*>(tarName);
        if (chkTarget)
            chkTarget->setStyleSheet(QString("color: %1; font-weight: bold;").arg(targetColors[i].name()));
        //绑定信号与槽函数
        connect(chkTarget, &QCheckBox::clicked, this, &ControlDialog::do_chkBoxClicked);
    }
    // 设置实际值复选框文本颜色
    for (int i = 0; i < 5; ++i)
    {
        QString actName = QString("chkAct%1").arg(i+1);
        QCheckBox* chkActual = findChild<QCheckBox*>(actName);
        if (chkActual)
            chkActual->setStyleSheet(QString("color: %1; font-weight: bold;").arg(actualColors[i].name()));
        //绑定信号与槽函数
        connect(chkActual, &QCheckBox::clicked, this, &ControlDialog::do_chkBoxClicked);
    }

}

ControlDialog::~ControlDialog()
{
    delete ui;
}

void ControlDialog::do_chkBoxClicked()
{
    // 获取发送信号的复选框
    QCheckBox* chk = qobject_cast<QCheckBox*>(sender());
    if (!chk) return;

    QString name = chk->objectName();
    int channel = -1;
    bool isTarget = false;

    // 解析 objectName，例如 "chkTar1" 或 "chkAct3"
    if (name.startsWith("chkTar")) 
    {
        isTarget = true;
        channel = name.mid(6).toInt() - 1;   // "chkTar1" -> 1 -> 索引0
    } else if (name.startsWith("chkAct")) 
    {
        isTarget = false;
        channel = name.mid(6).toInt() - 1;
    }

    if (channel < 0 || channel >= 5) return;

    // 更新对应的可见性数组
    if (isTarget) 
        m_targetVisible[channel] = chk->isChecked();
    else 
        m_actualVisible[channel] = chk->isChecked();
    

    // 发射信号，传递当前通道的两个可见性状态
    emit setChannelVisible(channel, m_targetVisible[channel], m_actualVisible[channel]);
}

void ControlDialog::on_comboColor_currentIndexChanged(int index)
{
    emit setBackColor(index);
}

void ControlDialog::on_comboTime_currentIndexChanged(int index)
{
    if(index == 0)  emit setAbsTime(false);
    else            emit setAbsTime(true);
}