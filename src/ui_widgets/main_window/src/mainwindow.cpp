#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <string>

#include "status_bar.h"
#include "chart.h"
#include "serial.h"
#include "chart_dialog.h"
#include "window_data_processor.h"

static void setLabelColor(QLabel *label, const QString &color);
static void initPidData(pid_data_t *data);

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    buildUI_StatusBar();
    buildChart();                       //先构造图表
    buildUI_SerialPort();               //再构造串口
    buildUI_Others();
}

MainWindow::~MainWindow()
{
    process_thread->quit();
    process_thread->wait();
    delete processor;
    delete ui;
}

void MainWindow::buildUI_StatusBar()       //状态栏UI
{
    m_status = new StatusBar;
    ui->statusbar->addPermanentWidget(m_status, 1);     //添加状态栏
}

void MainWindow::buildUI_SerialPort()
{
    serial_manager = new SerialManager(this);       
    processor = new WindowDataProcessor;
    process_thread = new QThread(this);
    processor->moveToThread(process_thread);  //将对象移动到线程
    processor->setManager(chart_manager);     //设置管理器
    processor->setPidNum(&pid_datas);         //设置PID数据
    //绑定信号与槽函数
    connect(processor, &WindowDataProcessor::pidActualChanged, this, &MainWindow::do_pidActualChanged);
    connect(processor, &WindowDataProcessor::textOrCMDReceived, this, &MainWindow::do_textOrCMDReceived);
    connect(serial_manager, &SerialManager::serialOpened, this, &MainWindow::do_serialOpened);
    connect(serial_manager, &SerialManager::serialClose, this, &MainWindow::do_serialClose);
    connect(serial_manager, &SerialManager::serialPortNumChanged, this, &MainWindow::do_serialPortNumChanged);
    connect(serial_manager, &SerialManager::serialDataUpdated, processor, &WindowDataProcessor::do_serialDataUpdated);
    //启动串口管理器
    serial_manager->start();
    process_thread->start();                  //数据处理线程启动
}

void MainWindow::buildDB()                    //数据库创建
{
    //m_sql = new SqlManager();
}

void MainWindow::buildChart()
{
    //设置5个通道
    chart_manager = new ChartManager(5, this);              //创建图表管理器，负责图表模块对象管理
    ui->chartView->installEventFilter(this);                //安装事件管理器，启动鼠标双击事件
    ui->chartView->setChartManager(chart_manager);          //配置图表管理器
    m_dialog = new ChartDialog(this);                       //图表控制对话框
    m_dialog->connectManager(chart_manager);                //信号与槽连接
    //绑定信号与槽
    connect(chart_manager, &ChartManager::exportDataFinished, this, &MainWindow::do_exportFinished);
    //启动图表管理器
    chart_manager->setPeriod(120);                          //设置图表刷新周期：120ms
    chart_manager->start();                                  
}

void MainWindow::buildUI_Others()          //其余部分的UI处理
{
    ui->toolBox->setCurrentIndex(0);       //设置PID调试界面为初始界面
    setLabelColor(ui->labOperate, "gray");
    // 获取当前文本光标
    QTextCursor cursor = ui->plainTextEdit->textCursor();
    // 将光标移动到文档末尾（防止覆盖已有内容）
    cursor.movePosition(QTextCursor::End);
    // 设置光标位置的字符格式为正常字体（例如 10pt）
    QTextCharFormat normalFormat;
    normalFormat.setFontPointSize(10);
    cursor.setCharFormat(normalFormat);
    // 将修改后的光标设回编辑器
    ui->plainTextEdit->setTextCursor(cursor);
    //初始化PID调试数据
    pid_datas.resize(5);                    //初始化5个PID通道
    for(int i = 0; i < pid_datas.size(); i++)
    {
        initPidData(&pid_datas[i]);         //手动初始化
    }
}

void MainWindow::on_btnClearRev_clicked()
{
    ui->plainTextEdit->clear();
}

void MainWindow::on_btnStart_clicked()                    //下位机启停按钮
{
    QString str = ui->btnStart->text();
    if(str == "启动")
        serial_manager->send(Data_CMD_Start, QByteArray()); //发送指令即可
    else
        serial_manager->send(Data_CMD_Stop, QByteArray());  //不需要数据
}

//串口数据处理相关接口
void MainWindow::do_textOrCMDReceived(uint8_t type, const QString& str)           //读取指令或文本，str为状态字符串 
{   
    switch(type)
    {
        case Data_CMD_Text:
        {
            ui->plainTextEdit->appendPlainText(str);
            break;
        }   //接收到通道数据
        case Data_Res_Start:                        //下位机响应启动
        {
            ui->btnStart->setText("暂停");
            m_status->setInfo(str);
            break;
        }
        case Data_Res_Stop:                         //下位机响应停止
        {
            ui->btnStart->setText("启动");          //
            m_status->setInfo(str);
            break;
        }
        default : break;
    }
}

//更新PID实际值
void MainWindow::do_pidActualChanged(int ch, const QString& actual)
{
    if(ch == ui->comboCh->currentIndex())
        ui->editActual->setText(actual);
}

void MainWindow::do_serialOpened(bool success, const QString& msg)
{
    if(success)
    {
        setLabelColor(ui->labOperate,"green");
        ui->btnSerial->setText("关闭串口");  
        m_status->setInfo(msg); 
        QMessageBox::information(this, "信息", msg);
    }
    else
    {      
        m_status->setInfo(msg); 
        QMessageBox::critical(this, "错误", msg); 
    }
}

void MainWindow::do_serialClose()
{
    QString msg = "关闭串口成功";
    QMessageBox::information(this, "信息", msg);
    m_status->setInfo(msg);
    ui->btnSerial->setText("打开串口");
    setLabelColor(ui->labOperate,"gray");                   //切换状态指示灯
}

void MainWindow::do_serialPortNumChanged(const QStringList& portNum)              //端口数量变化  
{
    ui->comboPort->clear();                                 //清空原有的端口数据
    ui->comboPort->addItems(portNum);                       //更新数据
}

void MainWindow::do_exportFinished(bool success, const QString& msg)
{
    m_status->setInfo(msg);
    if(success)
        QMessageBox::information(this, "信息", msg);
    else
        QMessageBox::warning(this, "警告", msg);
}

void MainWindow::on_btnSerial_clicked()
{
    if(ui->btnSerial->text() == "打开串口")
    {
        static SerialConfig config;                                     //配置串口信息
        config.port = ui->comboPort->currentIndex();                    //端口号
        config.stop = (uint8_t)(ui->comboStop->currentText().toInt());  //停止位
        config.data = (uint8_t)(ui->comboData->currentText().toInt());  //数据位 
        config.baud = (uint32_t)(ui->comboBaud->currentText().toInt()); //波特率
        int checkIdx = ui->comboCheck->currentIndex();                  //校验位
        config.check = (checkIdx == 0) ? 0 : (checkIdx == 1) ? 3 : 2;
        serial_manager->open(config);                                   //打开串口
    }
    else{
        serial_manager->close();                                        //关闭串口
    }
}

void MainWindow::on_btnSend_clicked()
{
    QString str = ui->editCMD->text();              //获取控制台指令
    serial_manager->send(Data_CMD_Text, str.toUtf8());
}

void MainWindow::on_btnDataExport_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "导出CSV数据", "", "CSV文件(*.csv)");
    if(fileName.isEmpty()) return;
    chart_manager->exportData(fileName, 0, 0);          //导出所有数据
}

void MainWindow::on_btnChartShow_clicked()
{
    m_dialog->show();
    m_status->setInfo("打开图表控制器");
}

void MainWindow::on_btnTarget_clicked()            //发送目标值
{
    int ch = ui->comboCh->currentIndex();
    if(ch > 4) return;
    QString str = ui->editTarget->text();
    float value = str.toFloat();
    pid_datas[ch].target = value;
    // 将 float 转换为二进制 QByteArray
    QByteArray data((const char*)&value, sizeof(value));
    serial_manager->send(Data_Target, data);
}

void MainWindow::on_btnMode_clicked()
{
    int mode = ui->comboMode->currentIndex();
    m_dialog->do_modeChanged(mode);
    m_status->setInfo("图表显示模式切换");
}

void MainWindow::on_btnPID_clicked()
{
    int ch = ui->comboCh->currentIndex();
    if(ch > 4) return;

    float pid[3];
    pid[0] = ui->editP->text().toFloat();
    pid[1] = ui->editI->text().toFloat();
    pid[2] = ui->editD->text().toFloat();
    // 保存PID参数
    pid_datas[ch].Kp = pid[0];
    pid_datas[ch].Ki = pid[1];
    pid_datas[ch].Kd = pid[2];
    // 将 float 转换为二进制 QByteArray
    QByteArray data((const char*)&pid, sizeof(pid));
    serial_manager->send(Data_CMD_Set_PID, data);
    m_status->setInfo("成功发送PID参数");
}

void MainWindow::on_btnPeriod_clicked()
{
    int ch = ui->comboCh->currentIndex();
    if(ch > 4) return;

    float period = ui->spinPeriod->value();
    // 保存P参数
    pid_datas[ch].dt = period;
    // 将 float 转换为二进制 QByteArray
    QByteArray data((const char*)&period, sizeof(period));
    serial_manager->send(Data_CMD_Set_Period, data);
    m_status->setInfo("设置PID周期(ms)");
}

void MainWindow::on_btnReset_clicked()
{
    serial_manager->send(Data_CMD_Reset, QByteArray());
    m_status->setInfo("发送复位命令");
    //界面也复位
    chart_manager->clearShow();
}

void MainWindow::on_btnClearShow_clicked()
{   //停止显示
    chart_manager->clearShow();
    m_status->setInfo("清空显示");
}

void MainWindow::on_btnStopShow_clicked()
{   //停止显示
    chart_manager->stopShow();
    m_status->setInfo("停止显示曲线");
}

void MainWindow::on_comboCh_currentIndexChanged(int index)
{   //更新通道对应数值
    if(index > 4) return;
    ui->editP->setText(QString("%1").arg(pid_datas[index].Kp));
    ui->editI->setText(QString("%1").arg(pid_datas[index].Ki));
    ui->editD->setText(QString("%1").arg(pid_datas[index].Kd));
    ui->editTarget->setText(QString("%1").arg(pid_datas[index].target));
    ui->editActual->setText(QString("%1").arg(pid_datas[index].actual));
    ui->spinPeriod->setValue(pid_datas[index].dt);
    m_status->setInfo("控制通道切换");
}

void MainWindow::on_btnHelp_clicked()
{   //避免重复创建字符串列表
    static const QStringList helpLines = {
        "LTM_Monitor上位机监控调试软件 ==> 帮助指令：",
        "1、配置好串口参数后,点击 '打开串口' 启动数据交互。",
        "2、点击 '图表显示' 按键进行图表显示控制，可设置背景色和显示时间等",
        "3、点击 '电机控制' 组件，切换到电机控制界面。",
        "4、点击 '模式'按键, 切换图表显示模式；双击图表进入'自动模式', '手动模式' 下可进行曲线平移和缩放操作"
    };
    ui->plainTextEdit->appendPlainText(helpLines.join("\n"));
}

void MainWindow::on_actImportDB_triggered()
{
    // QString aFile = QFileDialog::getOpenFileName(this, "选择文件","","SQLite数据库(*.db3)");
    // if(aFile.isEmpty())
    //     return;
    // if(m_sql->setDB(aFile))
    // {
    //     setLabelInfo(labInfo, "成功打开数据库");
    //     QMessageBox::warning(this, "信息", "打开数据库成功");
    // }
    // else
    // {
    //     setLabelInfo(labInfo, "打开数据库失败");
    //     QMessageBox::warning(this, "错误", "打开数据库失败");
    // }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == ui->chartView)
    {
        if(event->type() == QEvent::MouseButtonDblClick) 
        {
            ui->comboMode->setCurrentIndex(Mode_Auto);  //双击恢复自动模式
            m_dialog->do_modeChanged(Mode_Auto);
            return true;             //其他事件继续交由主窗口对象处理
        }
        if (event->type() == QEvent::MouseButtonPress) 
        {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && chart_manager->getMode() == Mode_Auto)
            {
                ui->comboMode->setCurrentIndex(Mode_Hand);
                m_dialog->do_modeChanged(Mode_Hand);
                // 不返回 true，让事件继续传递给 ChartView 以便开始平移
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

/******************************************************************************/

static void setLabelColor(QLabel *label, const QString &color)
{
    label->setText(QString("操作<span style='font-size:16px; color:%1;'>●</span>").arg(color));
}

static void initPidData(pid_data_t *data)
{
    if(data == nullptr) return;
    data->dt = 0;
    data->actual = 0;
    data->Kp = 0.000;
    data->Ki = 0.000;
    data->Kd = 0.000;
    data->target = 0;
}