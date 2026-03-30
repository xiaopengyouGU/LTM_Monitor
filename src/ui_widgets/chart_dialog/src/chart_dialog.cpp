#include "chart_dialog.h"
#include "chart_manager.h"
#include "ui_chart_dialog.h"

ChartDialog::ChartDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChartDialog)
{
    ui->setupUi(this);
    m_manager = nullptr;            //设置m_manager为nullptr
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
    for (int i = 0; i < 5; i++)
    {
        QString tarName = QString("chkTar%1").arg(i+1);
        QCheckBox* chkTarget = findChild<QCheckBox*>(tarName);
        if (chkTarget)
        {   //绑定信号与槽函数
            chkTarget->setStyleSheet(QString("color: %1; font-weight: bold;").arg(targetColors[i].name()));
            connect(chkTarget, &QCheckBox::clicked, this, &ChartDialog::do_chkBoxClicked);
        }
    }
    // 设置实际值复选框文本颜色
    for (int i = 0; i < 5; i++)
    {
        QString actName = QString("chkAct%1").arg(i+1);
        QCheckBox* chkActual = findChild<QCheckBox*>(actName);
        if (chkActual)
        {   //绑定信号与槽函数
            chkActual->setStyleSheet(QString("color: %1; font-weight: bold;").arg(actualColors[i].name()));
            connect(chkActual, &QCheckBox::clicked, this, &ChartDialog::do_chkBoxClicked);
        }
    }
    //绑定图例名修改信号与槽函数
    for(int i = 0; i < 5; i++)
    {
        QString editName = QString("editCH%1").arg(i+1);
        QLineEdit* editCH = findChild<QLineEdit*>(editName);
        if(editCH)
        {   //绑定信号与槽函数
            connect(editCH, &QLineEdit::textChanged, this, &ChartDialog::do_legendChanged);
        }
    }

}

ChartDialog::~ChartDialog()
{
    delete ui;
}

void ChartDialog::do_chkBoxClicked()
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
    

    if(!m_manager)  return;
    m_manager->setChannelVisible(channel, m_targetVisible[channel], m_actualVisible[channel]);
}

void ChartDialog::do_legendChanged(const QString& name)
{
    // 获取发送信号的对象
    QLineEdit* editCH = qobject_cast<QLineEdit*>(sender());
    if (!editCH) return;
    QString str = editCH->objectName();
    int ch = str.mid(6).toInt() - 1;   // "editCH1" -> 1 -> 索引0
    //发送当前通道的新图例名
    if(!m_manager)      return;
    m_manager->setLegendName(ch, name);
}

void ChartDialog::do_modeChanged(int mode)                       //更新dialog的当前模式
{
    if(mode < 0 || mode > 1) mode = 0;
    ui->comboMode->setCurrentIndex(mode);
    if(!m_manager)      return;                                  //判空
    m_manager->setMode(mode);                                    //发送信号
}

void ChartDialog::on_comboColor_currentIndexChanged(int index)
{
    if(!m_manager)      return;
    m_manager->setBackColor(index);
}

void ChartDialog::on_comboTime_currentIndexChanged(int index)
{
    if(!m_manager)      return;
    if(index == 0)      m_manager->setAbsTime(false);
    else                m_manager->setAbsTime(true);
}

void ChartDialog::on_comboWindow_currentIndexChanged(int index)
{   //切换窗口时间
    if(!m_manager)      return;     //判空
    int time = 30;
    switch (index)
    {
        case 0:
            time = 10;
            break;
        case 1:
            time = 30;
            break;
        case 2:
            time = 60;
            break;
        case 3:
            time = 5*60;
            break;
        case 4:
            time = 15*60;
            break;
        case 5:
            time = 30*60;
            break;
        case 6:
            time = 60*60;
            break;
    default:    break;
    }
    m_manager->setWindowTime(time);
}

void ChartDialog::on_comboMode_currentIndexChanged(int index)
{
    if(!m_manager)                          return;         //判空
    m_manager->setMode(index);
}

void ChartDialog::on_btnClearShow_clicked()
{
    if(!m_manager)                          return;         //判空
    m_manager->clearShow();
}

void ChartDialog::on_btnStopShow_clicked()
{
    for(int i = 0; i < 5; i++)
    {
        QString tarName = QString("chkTar%1").arg(i+1);
        QString actName = QString("chkAct%1").arg(i+1);
        QCheckBox* chkTarget = findChild<QCheckBox*>(tarName);
        QCheckBox* chkAcutal = findChild<QCheckBox*>(actName);
        chkTarget->setChecked(false);
        chkAcutal->setChecked(false);
    }
    if(!m_manager)      return;                                  //判空
    m_manager->stopShow();
}

void ChartDialog::connectManager(ChartManager *manager)                 //绑定信号到图表管理器
{
    if(!manager)        return;         //判空
    m_manager = manager;
}