#ifndef CHART_DIALOG_H
#define CHART_DIALOG_H

#include <QDialog>

class ChartManager;     //前向声明，加快编译速度

//这是chart模块的使用示例
namespace Ui {
class ChartDialog;
}

class ChartDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChartDialog(QWidget *parent = nullptr);
    ~ChartDialog();
public:
    void connectManager(ChartManager *manager);                 //绑定图表管理器
public slots:
    void do_chkBoxClicked();                                    //点击复选框
    void do_legendChanged(const QString& name);                 //图例名修改
    void do_modeChanged(int mode);                              //更新dialog的当前模式
private slots:
    void on_comboColor_currentIndexChanged(int index);
    void on_comboTime_currentIndexChanged(int index);
    void on_btnClearShow_clicked();
    void on_btnStopShow_clicked();
    void on_comboWindow_currentIndexChanged(int index);
    void on_comboMode_currentIndexChanged(int index);

private:
    Ui::ChartDialog *ui;
    ChartManager*   m_manager;
    bool m_targetVisible[5];
    bool m_actualVisible[5];
};

#endif // CHART_DIALOG_H
