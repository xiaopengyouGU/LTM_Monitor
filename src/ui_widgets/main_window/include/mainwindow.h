#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QDateTime>
#include <QTimer>
#include <QThread>
#include <QMessageBox>
#include <QFileDialog>
#include <QtCharts>
#include <QMouseEvent>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE


//该结构体用于数据库数据写入
typedef struct{
    qint64 timestamp;
    int channel;
    float target;
    float actual;
}data_point_t;

//该结构体用于记录PID调试界面信息
struct pid_data_t{
    float Kp;
    float Ki;
    float Kd;
    float dt;       //周期值
    float target;   //目标值
    float actual;   //实际值  
};


class ChartManager;             //前向声明
class SerialManager;
class StatusBar;
class ChartDialog;
class WindowDataProcessor;

class MainWindow : public QMainWindow
{
    Q_OBJECT

private:
    void buildUI();                    //UI创建
    // void buildMenu();               //菜单栏
    void buildDB();                    //数据库创建
    void buildChart();                 //绘图
    void buildUI_SerialPort();         //串口端口设置
    void buildUI_StatusBar();          //状态栏设置
    void buildUI_Others();             //其余部分的UI处理

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    bool eventFilter(QObject *obj, QEvent *event) override; //采用事件过滤器
private slots:

    void do_exportFinished(bool success, const QString& msg);
    //串口相关槽函数
    void do_serialPortNumChanged(const QStringList& portNum);              //端口数量变化  
    void do_serialOpened(bool success, const QString& msg);                //串口打开信号
    void do_serialClose();                                                 //串口关闭信号
    //串口数据处理相关接口
    void do_textOrCMDReceived(uint8_t type, const QString& str);           //读取指令或文本，str为状态字符串 
    void do_pidActualChanged(int ch, const QString& actual);

    void on_btnClearRev_clicked();
    void on_btnSend_clicked();                      //发送指令
    void on_btnSerial_clicked();
    void on_btnDataExport_clicked();                //CSV数据格式导出
    void on_btnChartShow_clicked();                 //打开图表控制器
    void on_btnStart_clicked();                     //下位机启停按钮
    void on_btnTarget_clicked();                    //发送目标值
    void on_btnMode_clicked();                      //显示模式切换
    void on_btnPID_clicked();                       //发送PID参数
    void on_btnPeriod_clicked();                    //发送控制周期
    void on_btnReset_clicked();                     //发送复位命令
    void on_btnStopShow_clicked();                  //停止显示所有曲线
    void on_btnClearShow_clicked();                 //清空显示,一键清屏
    void on_btnHelp_clicked();                      //输出帮助信息
    void on_comboCh_currentIndexChanged(int index); //当前通道切换

    //菜单栏对应的槽函数
    void on_actImportDB_triggered();                //打开数据库

private:
    Ui::MainWindow *ui;
   

private:
    StatusBar       *m_status;          //自定义状态栏
    ChartManager    *chart_manager;     //图表管理器
    ChartDialog     *m_dialog;          //图表控制对话框
    SerialManager   *serial_manager;    //串口管理器
    QThread         *process_thread;    //数据处理线程
    WindowDataProcessor *processor;     //窗口数据处理器
    QList<pid_data_t> pid_datas;        //保存了5个通道的目标值
};
#endif // MAINWINDOW_H
