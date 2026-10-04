#ifndef CHART_DIALOG_H
#define CHART_DIALOG_H

#include <QDialog>
#include <QList>

#if defined(CHART_DIALOG_LIBRARY)
#  define CHART_DIALOG_EXPORT Q_DECL_EXPORT
#else
#  define CHART_DIALOG_EXPORT Q_DECL_IMPORT
#endif

class ChartManager;     // 前向声明

// 图表控制对话框：通道列表（颜色/名字/实际值/可见）+ 视图/时间/背景切换。
class CHART_DIALOG_EXPORT ChartDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ChartDialog(QWidget *parent = nullptr);
    ~ChartDialog();

    void connectManager(ChartManager *manager, int channelCount);   // 绑定图表管理器与通道数

signals:
    void viewChanged(int viewIndex);                          // 视图页切换请求

public slots:
    void do_channelValues(const QList<double>& values);       // 中转站节流广播：各通道实际值

private slots:
    void on_comboTime_currentIndexChanged(int index);
    void on_comboColor_currentIndexChanged(int index);
    void on_comboRange_currentIndexChanged(int index);
    void on_btnClearShow_clicked();
    void on_btnStopShow_clicked();

private:
    Q_DISABLE_COPY(ChartDialog)
    class Private;
    Private *pimpl = nullptr;
};

#endif // CHART_DIALOG_H
