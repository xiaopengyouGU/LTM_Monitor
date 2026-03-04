#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QDateTime>
#include <QTimer>
#include <QHBoxLayout>
#include <QThread>
#include <QMessageBox>
#include <QFileDialog>
#include <QtCharts>
#include <QMouseEvent>

#include "data_processor.h"
#include "data_exporter.h"
#include "chart_manager.h"
#include "chart_view.h"
#include "control_dialog.h"
#include "data_def.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
private:
    //状态栏中的组件
    QLabel          *labAuthor;        //作者
    QLabel          *labWebside;       //网站
    QLabel          *labInfo;          //信息
    QLabel          *labTime;          //日期时间
    //显示当前日期和时间
    QTimer          *timer;            //软件定时器
    QDateTime       dateTime;          //日期期间显示

    void buildUI();                    //UI创建
    void buildThreads();               //线程处理
    // void buildMenu();               //菜单栏
    void buildChart();                 //绘图
    void buildUI_SerialPort();         //串口端口设置
    void buildUI_StatusBar();          //状态栏设置
    void buildSignalSlots();           //信号与槽
    void buildUI_Others();             //其余部分的UI处理

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    bool eventFilter(QObject *obj, QEvent *event) override; //采用事件过滤器
private slots:
    void do_dataReceived(uint8_t type, QByteArray data);

    void do_timer_timeout();
    void do_updateChart();                  //更新图表， 100ms一次
    void do_serialStatusChanged(uint8_t flag);
    void do_exportFinished(bool success, const QString& msg);
    void do_setChannelVisible(int channel, bool targetVisible, bool actualVisible);
    void do_setBackColor(int index);
    void do_setAbsTime(bool isAbs);

    void on_btnClearRev_clicked();
    void on_btnSend_clicked();              //发送指令
    void on_btnSerial_clicked();
    void on_btnDataExport_clicked();        //CSV数据格式导出
    void on_btnChartShow_clicked();         //打开图表控制器
    void on_btnStart_clicked();             //下位机启停按钮
    void on_btnTarget_clicked();            //发送目标值
    void on_btnMode_clicked();              //显示模式切换

signals:
    void sendData(uint8_t type, QByteArray data);
    void dataExport(const QString& fileName, qint64 startTime, qint64 endTime);
private:
    Ui::MainWindow *ui;
    QThread *data_thread;
    DataProcessor *processor;
    //与图表相关对象
    ChartManager *chart_manager;    //图表管理器，负责绘图与图表控制
    DataStorage *storage;           //图表数据存储
    QChart *m_chart;                //图表对象
    QTimer  *m_chartTimer;          //图表刷新定时器
    ControlDialog *m_dialog;        //图表控制器

private:
    pid_data_t pid_datas[5];        //保存了5个通道的目标值
    bool mode;                      //0:自动模式， 1:手动模式
};
#endif // MAINWINDOW_H
