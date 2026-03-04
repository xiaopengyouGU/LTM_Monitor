#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <string>

static void setLabelColor(QLabel *label, const QString &color);
static void setLabelInfo(QLabel *label, const QString &info);
static void addSeparator(QHBoxLayout* layout);
static void initPidData(pid_data_t *data);

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    buildUI_StatusBar();
    buildUI_SerialPort();
    buildChart();
    buildThreads();
    buildSignalSlots();
    buildUI_Others();
}

MainWindow::~MainWindow()
{
    if(data_thread->isRunning())
    {
        //调用处理器的stop槽，通知其退出循环
        QMetaObject::invokeMethod(processor, "stop", Qt::QueuedConnection);
        data_thread->quit();
        data_thread->wait();                //等待线程结束
    }
    delete processor;
    delete chart_manager;                   //先删除图表管理器        
    delete m_chart;                         //再删除图表
    delete storage;                 
    delete ui;
}

void MainWindow::buildUI_StatusBar()       //状态栏UI
{
    //时间日期显示
    dateTime = QDateTime::currentDateTime();
    QString str = dateTime.toString("yyyy/MM/dd  hh:mm:ss");

    QWidget *statusWidget = new QWidget(this);
    QHBoxLayout *statusLayout = new QHBoxLayout(statusWidget);   // 父对象为 statusWidget
// 然后向 statusLayout 添加标签...
    statusLayout->setContentsMargins(6, 2, 6, 2); // 设置内边距，让显示更舒适
    statusLayout->setSpacing(15);                 // 设置标签间的间距

    labAuthor = new QLabel(this);
    labAuthor->setMinimumWidth(100);
    labAuthor->setText("  Lvtou (中南大学)");
    statusLayout->addWidget(labAuthor);        //添加标签到水平布局
    addSeparator(statusLayout);                //添加分隔条

    labWebside = new QLabel(this);
    labWebside->setMinimumWidth(300);
    labWebside->setText(" https://github.com/xiaopengyouGU/LTM_Monitor");
    statusLayout->addWidget(labWebside,1);        //添加标签到水平布局
    addSeparator(statusLayout);                //添加分隔条
    labInfo = new QLabel(this);
    labInfo->setMinimumWidth(300);
    labInfo->setText("欢迎使用: LTM_Monitor V0.1.0");
    statusLayout->addWidget(labInfo,3);        //添加标签到水平布局
    addSeparator(statusLayout);                //添加分隔条

    labTime = new QLabel(this);
    labTime->setMinimumWidth(100);
    labTime->setText(str);
    statusLayout->addWidget(labTime);        //添加标签到水平布局

    statusWidget->setLayout(statusLayout);
    ui->statusbar->addPermanentWidget(statusWidget,1);

    //周期定时器创建，用于显示当前日期和时间
    timer = new QTimer(this);
    timer->stop();
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(1000);                   //定时周期1000ms,即1s
    timer->setSingleShot(false);                //连续定时
    //绑定完槽函数后再启动定时器
}

void MainWindow::buildUI_SerialPort()
{
    QStringList currentPorts;
    static QStringList m_lastPorts;
    foreach (const QSerialPortInfo &portInfo, QSerialPortInfo::availablePorts()) {
        currentPorts << portInfo.portName() + ":" + portInfo.description();
    }
    // 比较是否与上次相同
    if (currentPorts == m_lastPorts)
        return;
    // 更新 comboBox
    ui->comboPort->clear();
    ui->comboPort->addItems(currentPorts);
    // 保存当前列表
    m_lastPorts = currentPorts;    
}

void MainWindow::buildThreads()
{
    //创建数据线程和处理器对象
    data_thread = new QThread(this);
    processor = new DataProcessor();
    //将处理器对象移动到线程
    processor->moveToThread(data_thread);
}

void MainWindow::buildChart()
{
    m_chart = new QChart();
    storage = new DataStorage(720000);
    chart_manager = new ChartManager(m_chart);
    m_dialog = new ControlDialog(this);     //创建图表控制器
    ui->chartView->setChart(m_chart);
    ui->chartView->setRenderHint(QPainter::Antialiasing);   //启用抗锯齿，让曲线滚动流畅
    ui->chartView->installEventFilter(this);                //安装事件过滤器，启动鼠标双击事件
    ui->chartView->setChartManager(chart_manager);          //配置图表管理器
    //设置窗口刷新定时器 
    m_chartTimer = new QTimer(this);
    m_chartTimer->stop();
    m_chartTimer->setTimerType(Qt::PreciseTimer);
    m_chartTimer->setInterval(100);                    //定时周期100ms
    m_chartTimer->setSingleShot(false);                //连续定时
}

void MainWindow::buildSignalSlots()        //手动关联信号与槽函数
{
    connect(timer,SIGNAL(timeout()),this,SLOT(do_timer_timeout()));
    timer->start();                         //启动状态栏定时器

    connect(processor, &DataProcessor::dataReceived, this, &MainWindow::do_dataReceived);
    connect(processor, &DataProcessor::serialStatusChanged, this, &MainWindow::do_serialStatusChanged);
    connect(this, &MainWindow::sendData, processor, &DataProcessor::do_sendData);
    // 在主线程中将串口对象移动到数据线程
    moveSerialToThread(data_thread);    //只需调用1次
    //
    connect(m_chartTimer, &QTimer::timeout, this, &MainWindow::do_updateChart);
    m_chartTimer->start();                  //启动图表刷新定时器
    //绑定显示控制器
    connect(m_dialog, &ControlDialog::setChannelVisible, this, &MainWindow::do_setChannelVisible);
    connect(m_dialog, &ControlDialog::setBackColor, this, &MainWindow::do_setBackColor);
    connect(m_dialog, &ControlDialog::setAbsTime, this, &MainWindow::do_setAbsTime);
    //双击屏幕，切换为自动模式, 由事件过滤器处理
}

void MainWindow::buildUI_Others()          //其余部分的UI处理
{
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
    for(int i = 0; i < 5; i++)
        initPidData(&pid_datas[i]);
    mode = 0;                               //图表默认为自动模式
}

void MainWindow::on_btnClearRev_clicked()
{
    ui->plainTextEdit->clear();
}

void MainWindow::on_btnStart_clicked()                    //下位机启停按钮
{
    QString str = ui->btnStart->text();
    if(str == "启动")
        emit sendData(Data_CMD_Start, str.toUtf8());
    else
        emit sendData(Data_CMD_Stop, str.toUtf8());
}

void MainWindow::do_updateChart()              //更新图表， 200ms一次
{
    if(chart_manager && storage)
        chart_manager->updateData(storage);
}

void MainWindow::do_timer_timeout()
{
    dateTime = QDateTime::currentDateTime();
    QString str = dateTime.toString("yyyy/MM/dd  hh:mm:ss");
    labTime->setText(str);

    //检测串口是否有更新
    static uint64_t count = 0;
    count++;
    if (count % 2 == 0) //2s检测一次
    {
        buildUI_SerialPort();
    }
}

void MainWindow::do_dataReceived(uint8_t type, QByteArray data)
{
    //接收到数据了,根据接收到的数据类型直接进行处理（暂时）
    switch(type)
    {
        case Data_CMD_Text:
        {
            QString str = QString::fromUtf8(data);
            ui->plainTextEdit->appendPlainText(str);
            break;
        }   //接收到通道数据
        case Data_Channel1:
        case Data_Channel2:
        case Data_Channel3:
        case Data_Channel4:
        case Data_Channel5:
        {
            int ch = type - Data_Channel1;  //通道枚举值是连续递增的
            float actual;
            memcpy(&actual, data.constData(), data.size());
            float target = pid_datas[ch].target;    //此处的ch取值 0 - 4 开始的，实际对应通道CH1——CH5      
            storage->addData(ch, target, actual);   //存储数据
            break;
        }
        case Data_Res_Start:                        //下位机响应启动
        {
            ui->btnStart->setText("暂停");
            setLabelInfo(labInfo, "下位机启动成功");
            break;
        }
        case Data_Res_Stop:                         //下位机响应停止
        {
            ui->btnStart->setText("启动");          //
            setLabelInfo(labInfo, "下位机停机成功");
            break;
        }
        default : break;

    }
}

void MainWindow::do_serialStatusChanged(uint8_t flag)
{
    if(flag == 0)
    {
        setLabelColor(ui->labOperate,"green");
        setLabelInfo(labInfo, "打开串口成功");
        ui->btnSerial->setText("关闭串口");   
        QMessageBox::information(this, "提示信息", "串口已被成功打开");
    }
    else if(flag == 1)
    {      
        setLabelColor(ui->labOperate,"gray");
        setLabelInfo(labInfo, "关闭串口成功");
        ui->btnSerial->setText("打开串口");
        QMessageBox::information(this, "提示信息", "串口已被成功关闭"); 
    }
    else 
    {
        setLabelInfo(labInfo, "打开串口失败");
        QMessageBox::critical(this, "错误", "串口打开失败");
    }
}void do_exportFinished(bool success, const QString& msg);

void MainWindow::do_exportFinished(bool success, const QString& msg)
{
    setLabelInfo(labInfo, msg);
    if(success)
        QMessageBox::information(this, "导出完成", msg);
    else
        QMessageBox::warning(this, "导出失败", msg);
}

void MainWindow::do_setChannelVisible(int channel, bool targetVisible, bool actualVisible)
{
    chart_manager->setChannelVisible(channel, targetVisible, actualVisible);
    setLabelInfo(labInfo, "图表显示变化");
}

void MainWindow::do_setBackColor(int index)
{
    chart_manager->setBackColor(index);
    setLabelInfo(labInfo, "图表背景颜色变化");
}

void MainWindow::do_setAbsTime(bool isAbs)
{
    chart_manager->setAbsTime(isAbs);
    if(isAbs)
        setLabelInfo(labInfo, "设置图表以绝对时间显示");
    else
        setLabelInfo(labInfo, "设置图表以相对时间显示");
}

void MainWindow::on_btnSerial_clicked()
{
    if(ui->btnSerial->text() == "打开串口")
    {
        serial_config_t config;                                         //配置串口信息
        config.port = ui->comboPort->currentIndex();                    //端口号
        config.stop = (uint8_t)(ui->comboStop->currentText().toInt());  //停止位
        config.data = (uint8_t)(ui->comboData->currentText().toInt());  //数据位 
        config.baud = (uint32_t)(ui->comboBaud->currentText().toInt()); //波特率
        int checkIdx = ui->comboCheck->currentIndex();                  //校验位
        config.check = (checkIdx == 0) ? 0 : (checkIdx == 1) ? 3 : 2;
        
        processor->setSerialPortConfig(&config);
        data_thread->start();
        QMetaObject::invokeMethod(processor, "openSerial", Qt::QueuedConnection);
    }
    else
    {
        // 关闭串口
        QMetaObject::invokeMethod(processor, "stop", Qt::QueuedConnection);
    }
}

void MainWindow::on_btnSend_clicked()
{
    QString str = ui->editCMD->text();              //获取控制台指令
    emit sendData(Data_CMD_Text, str.toUtf8());
}

void MainWindow::on_btnDataExport_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "导出CSV数据", "", "CSV文件(*.csv)");
    if(fileName.isEmpty()) return;
    //创建一个临时线程负责数据导出
    QThread *thread = new QThread;
    DataExporter *exporter = new DataExporter(storage);
    exporter->moveToThread(thread);

    //清零线程和exporter, 自动释放内存
    connect(thread, &QThread::finished, exporter, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    //连接导出结果处理, 并启动导出
    connect(exporter, &DataExporter::exportFinished, this, &MainWindow::do_exportFinished);
    connect(this, &MainWindow::dataExport, exporter, &DataExporter::do_dataExport);
    thread->start();
    emit dataExport(fileName, 0, 0);            //0,0 表示导出全数据
}

void MainWindow::on_btnChartShow_clicked()
{
    m_dialog->show();
    setLabelInfo(labInfo, "打开显示控制器");
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
    emit sendData(Data_Target, data);
}

void MainWindow::on_btnMode_clicked()
{
    int mode = ui->comboMode->currentIndex();
    chart_manager->setMode(mode);               //模式切换
    setLabelInfo(labInfo, "图表显示模式切换");
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == ui->chartView)
    {
        if(event->type() == QEvent::MouseButtonDblClick) 
        {
            ui->comboMode->setCurrentIndex(0);  //双击恢复自动模式
            on_btnMode_clicked();
            return true;             //其他事件继续交由主窗口对象处理
        }
        if (event->type() == QEvent::MouseButtonPress) 
        {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && chart_manager->getMode() == 0)
            {
                ui->comboMode->setCurrentIndex(1);
                on_btnMode_clicked();
                // 不返回 true，让事件继续传递给 ChartView 以便开始平移
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

/******************************************************************************/
/* 静态函数 */
static void addSeparator(QHBoxLayout* layout)
{
    QFrame *frame = new QFrame();
    frame->setFrameShape(QFrame::VLine); // 垂直线
    frame->setFixedWidth(2);
    layout->addWidget(frame);
}

static void setLabelColor(QLabel *label, const QString &color)
{
    label->setText(QString("操作<span style='font-size:16px; color:%1;'>●</span>").arg(color));
}

static void setLabelInfo(QLabel *label, const QString &info)
{
    QString str = QString("状态：") + info;
    label->setText(str);
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