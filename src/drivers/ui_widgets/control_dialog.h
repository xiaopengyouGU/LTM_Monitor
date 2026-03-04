#ifndef CONTROL_DIALOG_H
#define CONTROL_DIALOG_H

#include <QDialog>

namespace Ui {
class ControlDialog;
}

class ControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ControlDialog(QWidget *parent = nullptr);
    ~ControlDialog();

public slots:
    void do_chkBoxClicked();
private slots:
    void on_comboColor_currentIndexChanged(int index);
    void on_comboTime_currentIndexChanged(int index);
signals:
    void setChannelVisible(int channel, bool targetVisible, bool actualVisible);
    void setBackColor(int index);
    void setAbsTime(bool isAbs);
private:
    Ui::ControlDialog *ui;
    bool m_targetVisible[5];
    bool m_actualVisible[5];
};

#endif // CONTROL_DIALOG_H
