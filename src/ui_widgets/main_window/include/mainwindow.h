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

#define MAX_CHANNEL_SIZE        5       // 上位机最大显示通道数据（目标值+实际值）

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
class RecordManager;
class LogAnalysis;                          
class StatusBar;
class ChartDialog;
class CanfdWidget;
class CanfdManager;
struct CanfdConfig;
class WindowDataProcessor;

class MainWindow : public QMainWindow
{
    Q_OBJECT

private:
    void buildUI();                    // UI创建
    void buildRecord();                // 日志与数据库创建
    void buildChart();                 // 绘图
    void buildUI_Canfd();              // CAN-FD 模块设置
    void buildUI_SerialPort();         // 串口端口设置
    void buildUI_StatusBar();          // 状态栏设置
    void build_DataProcessor();        // 数据处理器创建
    void buildUI_Others();             // 其余部分的UI处理

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;      // 采用事件过滤器
private slots:

    void do_exportFinished(bool success, const QString& msg);    // 通道数据导出完毕 
    //串口相关槽函数
    void do_serialPortNumChanged(const QStringList& portNum);    // 端口数量变化  
    void do_serialOpened(bool success, const QString& msg);      // 串口打开信号
    void do_serialClose();                                       // 串口关闭信号
    // CAN-FD related slots
    void do_canfdOpened(bool success, const QString& msg);       // CAN-FD 打开信号       
    void do_canfdClosed();                                       // CAN-FD 关闭信号
    void do_canfdError(const QString& msg);                      // CAN-FD 故障信号
    void do_canfdOnlineChanged(bool online);                     // 设备状态切换
    void do_canfdBusError(uint32_t errCode, int channel);        // CAN-FD 总线错误
    void do_canfdDropped(int dropped, uint64_t total);           // 数据溢出
    // 串口数据处理相关接口
    void do_textOrCMDReceived(uint8_t type, const QString& str, const QByteArray& data); //读取指令或文本，str为状态字符串
    void do_pidActualChanged(int ch, const QString& actual);
    void do_pidActualChanged(const QStringList& actNum);

    void on_btnClearRev_clicked();
    void on_btnSend_clicked();                      // 发送指令
    void on_btnSerial_clicked();                    // 打开 串口
    void on_btnCanfd_clicked();                     // 打开 CAN-FD
    void on_btnDataExport_clicked();                // CSV数据格式导出
    void on_btnChartShow_clicked();                 // 打开图表控制器
    void on_btnStart_clicked();                     // 下位机启停按钮
    void on_btnTarget_clicked();                    // 发送目标值
    void on_btnMode_clicked();                      // 显示模式切换
    void on_btnPID_clicked();                       // 发送PID参数
    void on_btnPeriod_clicked();                    // 发送控制周期
    void on_btnReset_clicked();                     // 发送复位命令
    void on_btnStopShow_clicked();                  // 停止显示所有曲线
    void on_btnClearShow_clicked();                 // 清空显示,一键清屏
    void on_btnHelp_clicked();                      // 输出帮助信息
    void on_comboCh_currentIndexChanged(int index); // 当前通道切换
    void on_btnClearRecv_clicked();                 // 清空控制台
    void on_btnStopRecv_clicked();                  // 暂停控制接收
    
    //通讯调试界面组件
    void on_comboProt_currentIndexChanged(int index); // 更换通讯协议
    void on_chkSendPeriod_stateChanged(int arg1);     // 周期发送数据
    void on_chkSendNewL_stateChanged(int arg1);       // 发送新行
    void on_chkHexShow_stateChanged(int arg1);        // 16进制显示
    void on_chkHexSend_stateChanged(int arg1);        // 16进制发送
    void on_btnSaveCmd_clicked();                     // 保存控制台
    void on_btnOpenFile_clicked();                    // 打开文件
    void on_btnSendFile_clicked();                    // 发送文件

    //菜单栏对应的槽函数
    void on_actImportDB_triggered();                  // 打开数据库
    void on_actOpenLog_triggered();                   // 打开日志分析器
    void on_actUseIntro_triggered();                  // 使用说明
    void on_actShowCanfd_triggered(bool checked);     // 切换到 CAN-FD 窗口
    void on_actTerminalUtf8_triggered();              // 控制台编码：UTF-8
    void on_actTerminalGbk_triggered();               // 控制台编码：GBK
private:
    QString fromHexDisplay(const QString &hexDisplay);// 原始值转换到16进制
    QString toHexDisplay(const QString &rawText);     // 16进制到原始值
    QString decodeTerminal(const QByteArray &data) const;   // 控制台解码（按所选编码）
    QByteArray buildSendData();                       // 构造发送数据
    void appendText(const QString &text);             // 追加文本
    void updateDisplay();                             // 更新显示

private:
    Ui::MainWindow *ui;
   
private:
    StatusBar       *m_status;          // 自定义状态栏
    ChartManager    *chart_manager;     // 图表管理器
    ChartDialog     *m_dialog;          // 图表控制对话框
    CanfdWidget     *canfd_widget;
    CanfdManager    *canfd_manager;     // CAN-FD管理器
    SerialManager   *serial_manager;    // 串口管理器
    
    RecordManager   *record_manager;    // 记录管理器
    LogAnalysis     *log_analysis;      // 日志分析器
    
    QThread         *process_thread;    // 数据处理线程
    WindowDataProcessor *processor;     // 窗口数据处理器
    QList<pid_data_t> pid_datas;        // 保存了5个通道的目标值
    //与通讯调试界面相关的变量
    bool            m_recv;             // 接收标志
    bool            m_newLine;          // 是否发送新行（加换行符）
    bool            m_hexSendMode;      // 当前是否处于16进制发送模式
    bool            m_hexShowMode;      // 当前是否处于16进制接收模式
    QByteArray      m_rawReceivedData;  // 保存所有接收到的原始数据（二进制）
    QByteArray      m_fileData;         // 存储打开的文件数据
    int             m_terminalCodec;    // 控制台编码：0=UTF-8，1=GBK
};
#endif // MAINWINDOW_H
