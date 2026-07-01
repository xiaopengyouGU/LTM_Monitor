#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <string>

#include "status_bar.h"
#include "chart.h"
#include "serial.h"
#include "record.h"
#include "log_analysis.h"
#include "chart_dialog.h"
#include "window_data_processor.h"

#include <QTimer>
#include <QFileDialog>

#define LOG_DEBUG(msg)  {if(record_manager) record_manager->logDebug(msg);}
#define LOG_INFO(msg)   {if(record_manager) record_manager->logInfo(msg);}
#define LOG_WARN(msg)   {if(record_manager) record_manager->logWarn(msg);}
#define LOG_ERROR(msg)  {if(record_manager) record_manager->logError(msg);}

static void setLabelColor(QLabel *label, const QString &color);
static void initPidData(pid_data_t *data);
static QString decodeData(const QByteArray &data);          //动态解码，仅用于控制台中文接收


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    buildUI_StatusBar();
    buildRecord();                      //构造日志和数据库
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
    processor->moveToThread(process_thread);  //将处理器对象移动到线程
    processor->setManager(chart_manager);     //设置管理器
    processor->setPidNum(&pid_datas);         //设置PID数据
    //绑定信号与槽函数
    connect(processor, (qOverload<int, const QString&>)(&WindowDataProcessor::pidActualChanged), this, (qOverload<int, const QString&>)(&MainWindow::do_pidActualChanged));
    connect(processor, (qOverload<const QStringList& >)(&WindowDataProcessor::pidActualChanged), this, (qOverload<const QStringList&>)(&MainWindow::do_pidActualChanged));
    connect(processor,      &WindowDataProcessor::textOrCMDReceived, this, &MainWindow::do_textOrCMDReceived);
    connect(serial_manager, &SerialManager::serialOpened, this, &MainWindow::do_serialOpened);
    connect(serial_manager, &SerialManager::serialClose, this, &MainWindow::do_serialClose);
    connect(serial_manager, &SerialManager::serialPortNumChanged, this, &MainWindow::do_serialPortNumChanged);
    connect(serial_manager, &SerialManager::serialDataUpdated, processor, &WindowDataProcessor::do_serialDataUpdated);
    //启动串口管理器
    serial_manager->start();
    process_thread->start();               //数据处理线程启动
}

void MainWindow::buildRecord()             //日志与数据库创建
{
    record_manager = new RecordManager(this);
    log_analysis = new LogAnalysis(this);
    log_analysis->connectManager(record_manager);          //绑定记录管理器
    log_analysis->close();                                 //先不要显示
    record_manager->start();                               //启动记录管理器
   // LOG_DEBUG("测试日志系统");
   // LOG_INFO("日志系统初始化完毕");
}

void MainWindow::buildChart()
{   //设置5个通道
    chart_manager = new ChartManager(5, this);              //创建图表管理器，负责图表模块对象管理
    ui->chartView->installEventFilter(this);                //安装事件管理器，启动鼠标双击事件
    ui->chartView->setChartManager(chart_manager);          //配置图表管理器
    m_dialog = new ChartDialog(this);                       //图表控制对话框
    m_dialog->connectManager(chart_manager);                //信号与槽连接
    //绑定信号与槽
    connect(chart_manager, &ChartManager::exportDataFinished, this, &MainWindow::do_exportFinished);
    //启动图表管理器
    chart_manager->setPeriod(100);                          //设置图表刷新周期：100ms
    chart_manager->start();                                  
}

void MainWindow::buildUI_Others()          //其余部分的UI处理
{
    ui->toolBox->setCurrentIndex(0);       //设置PID调试界面为初始界面
    m_recv = true;                         //串口的数据接受是默认的
    m_newLine = true;                      //发送指令时，默认加换行符
    m_hexSendMode = false;                 //当前是否处于16进制发送模式
    m_hexShowMode = false;                 //当前是否处于16进制显示模式
    
    //该定时器用于周期发送数据
    m_timer = new QTimer(this);            
    m_timer->setSingleShot(false);
    m_timer->setInterval(100);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->stop();
    //绑定定时器回调函数
    connect(m_timer, &QTimer::timeout, this, &MainWindow::do_timer_timeout);

    setLabelColor(ui->labOperate, "gray");
    // 获取当前文本光标
    QTextCursor cursor = ui->plainTextEdit->textCursor();
    // 将光标移动到文档末尾（防止覆盖已有内容）
    cursor.movePosition(QTextCursor::End);
    // 设置光标位置的字符格式为正常字体（例如 11pt）
    QTextCharFormat normalFormat;
    normalFormat.setFontPointSize(11);
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
    QFont font = ui->plainTextEdit->font();
    font.setPointSize(11);                 //默认字体大小 11pt
    ui->plainTextEdit->setFont(font);
    m_rawReceivedData.clear();            //保存的原始数据也清空
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
void MainWindow::do_textOrCMDReceived(uint8_t type, const QString& str, const QByteArray& data)  //读取指令或文本，str为状态字符串
{
    switch(type)
    {
        case Data_CMD_Text:
        {
            if(!m_recv)     return;         //暂停控制台接收时，直接返回
            static bool isFirst = false;    //第一次调用时，直接append，不然字体会变得很大
            m_rawReceivedData.append(data); //直接采用原始二进制值

            if(!isFirst){
                //文本显示模式：自动检测编码（UTF-8 优先，失败则 GBK）
                QString displayText = decodeData(data);
                ui->plainTextEdit->appendPlainText(displayText);
                isFirst = true;
                return;
            }
            //添加新的数据（不自动换行）
            if(m_hexShowMode){              //16进制显示
                QString hexStr = data.toHex(' ').toUpper();
                appendText(hexStr+"\n");
            }else{
                //文本显示模式：自动检测编码（UTF-8 优先，失败则 GBK）
                QString displayText = decodeData(data);
                appendText(displayText);
            }
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

void MainWindow::do_pidActualChanged(const QStringList& actNum)
{
    int ch = ui->comboCh->currentIndex();
    if(ch >= actNum.size())             return;
    QString str = actNum[ch];           /* 最多通道 ch0 —— ch4 */
    ui->editActual->setText(str);
}

void MainWindow::do_serialOpened(bool success, const QString& msg)
{
    if(success)
    {
        setLabelColor(ui->labOperate,"green");
        ui->btnSerial->setText("关闭串口");  
        m_status->setInfo(msg); 
        QMessageBox::information(this, "信息", msg);
        LOG_INFO("打开串口成功");
    }
    else
    {      
        m_status->setInfo(msg); 
        QMessageBox::critical(this, "错误", msg); 
        LOG_ERROR("打开串口失败");
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

void MainWindow::do_timer_timeout()
{
    if(m_strPeriod.isEmpty()) return;
    serial_manager->send(Data_CMD_Text, m_strPeriod.toUtf8());
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
    // m_rawText 在用户输入指令时，动态调整（原始文本值）
    if(m_rawText.isEmpty())                        return; //判空
    QByteArray sendData;
    if(m_hexSendMode){
        // 十六进制发送模式：解析十六进制文本 → 实际字节（UTF-8编码，兼容ASCII码）
        QString hexStr = ui->editCMD->text();
        hexStr.remove(' ');                         //去除空格
        sendData = QByteArray::fromHex(hexStr.toUtf8());
        if(sendData.isEmpty() && !hexStr.isEmpty()){
            m_status->setInfo("无效的十六进制输入");
            return;
        }
        m_status->setInfo("十六进制数据发送成功");
    }else{
        // 原始数据发送模式：直接转 UTF-8
        sendData = m_rawText.toUtf8();
        m_status->setInfo("原始数据发送成功");      // ✅ 加这行
    }
    if(m_newLine){
        sendData.append("\r\n");              //发送新行，自动添加回车和换行符
        m_strPeriod = m_rawText + "\r\n";
    }else{
        m_strPeriod = m_rawText;
    }
    serial_manager->send(Data_CMD_Text, sendData);
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
        "2、点击 '图表显示' 按键进行图表显示控制，可设置背景色和显示时间等。",
        "3、点击 '电机控制' 组件，切换到电机控制界面。",
        "4、点击 '模式'按键, 切换图表显示模式；双击图表进入'自动模式', '手动模式' 下可进行曲线平移和缩放操作。"
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

void MainWindow::on_actOpenLog_triggered()                 //打开日志分析器
{
    log_analysis->show();                                  
    m_status->setInfo("打开日志分析器");
}

void MainWindow::on_actUseIntro_triggered()
{
    on_btnHelp_clicked();           //与帮助按键的输出一致
}

void MainWindow::on_btnClearRecv_clicked()
{
    on_btnClearRev_clicked();
}

void MainWindow::on_btnStopRecv_clicked()
{
    QString str = ui->btnStopRecv->text();
    if(str == "暂停接收"){
        m_status->setInfo("控制台已暂停接收！");
        ui->btnStopRecv->setText("继续接收");
        m_recv = false;
    }else{
        m_status->setInfo("控制台可继续接收！");
        ui->btnStopRecv->setText("暂停接收");
        m_recv = true;
    }
}

void MainWindow::on_comboProt_currentIndexChanged(int index)
{
    serial_manager->setProtocol(index);
    if(index == 0)  m_status->setInfo("当前通讯协议 ==》LTM协议！");
    else            m_status->setInfo("当前通讯协议 ==》普通串口！");
}

void MainWindow::on_chkSendPeriod_stateChanged(int arg1)
{
    if(arg1 == Qt::Checked){
        m_timer->stop();                            //先关闭定时器
        int period = ui->spinSendPeriod->value();
        m_timer->setInterval(period);
        m_timer->start();                           //启动定时器
        on_btnSend_clicked();                       //发送命令并保存
        m_status->setInfo("周期发送命令！");
    }else{
        m_timer->stop();
        m_status->setInfo("取消周期命令发送！");
    }
}

void MainWindow::on_chkSendNewL_stateChanged(int arg1)
{
    //是否发送新行
    if(arg1 == Qt::Checked){
        m_status->setInfo("指令输出自动添加回车换行符！");
        m_newLine = true;
    }else{
        m_newLine = false;
        m_status->setInfo("正常指令输出！");
    }
}

void MainWindow::on_chkHexShow_stateChanged(int arg1)
{
    if(arg1 == Qt::Checked){
        m_hexShowMode = true;  // 记录状态
        m_status->setInfo("控制台接收以 16进制 显示！");
        // 把已经收到的数据重新按十六进制显示，调用刷新函数
        updateDisplay();
    }else{
        m_hexShowMode = false;
        m_status->setInfo("控制台接收正常显示！");
        // 刷新显示
        updateDisplay();
    }
}

void MainWindow::on_chkHexSend_stateChanged(int arg1)
{
    m_hexSendMode = (arg1 == Qt::Checked) ? 1 : 0;

    if (m_hexSendMode) {
        // 切换到十六进制显示模式（UTF-8编码，兼容ASCII码）
        // 把 m_rawText 显示为十六进制格式
        QString displayText = toHexDisplay(m_rawText);
        ui->editCMD->setText(displayText);
        m_status->setInfo("发送十六进制数据！");
    } else {
        // 切换回普通文本显示模式
        // 注意：此时 ui->editCMD->text() 显示的是十六进制格式（如 "A5 B9"）
        // 需要尝试把十六进制字符串还原为原始文本
        QString currentText = ui->editCMD->text().trimmed();
        QString originalText = fromHexDisplay(currentText);

        // 如果还原成功，用还原后的文本；否则用 m_rawText
        if (!originalText.isEmpty()) {
            m_rawText = originalText;
        }
        ui->editCMD->setText(m_rawText);
        m_status->setInfo("发送原始数据！");
    }
}

void MainWindow::on_editCMD_textChanged(const QString &text)
{
    if (m_hexSendMode) {
        // 十六进制模式下，用户编辑的是十六进制文本
        // 尝试还原为原始文本
        QString original = fromHexDisplay(text);
        if (!original.isEmpty()) {
            m_rawText = original;
        }
    } else {
        // 普通模式下，直接更新原始文本
        m_rawText = text;
    }
}

void MainWindow::on_btnSaveCmd_clicked()
{
    if (ui->plainTextEdit->toPlainText().isEmpty()) {
        m_status->setInfo("没有内容可保存！");
        return;
    }

    // ✅ 获取当前可执行文件所在目录
    QString appPath = QCoreApplication::applicationDirPath();

    // 生成带时间戳的默认文件名
    QString defaultFileName = QString("console_log_%1.txt")
                                  .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    QString filePath = QDir(appPath).filePath(defaultFileName);

    // 打开保存对话框，默认路径为当前项目所在目录
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "保存控制台输出",
        filePath,
        "文本文件 (*.txt);;所有文件 (*)"
        );

    if (fileName.isEmpty()) {
        m_status->setInfo("保存已取消");
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_status->setInfo("无法创建文件: " + file.errorString());
        return;
    }

    QTextStream out(&file);
    out << ui->plainTextEdit->toPlainText();
    file.close();

    m_status->setInfo("控制台输出已保存至: " + fileName);
}

void MainWindow::on_btnSendFile_clicked()
{
    // 检查是否在普通串口协议模式下
    if (!ui->comboProt->currentIndex()){  // 0: LTM 协议
        QMessageBox::information(
            this,
            "协议不支持",
            "LTM 协议不支持文件发送功能！\n"
            "请切换到「普通串口」协议后再试。"
            );
        return;
    }

    if(m_fileData.isEmpty()) {
        m_status->setInfo("请先打开文件！");
        return;
    }

    if (ui->btnSerial->text() != "关闭串口"){
        m_status->setInfo("串口未打开，无法发送文件！");
        return;
    }

    // 确认发送（防止误触）
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "确认发送",
        QString("即将发送文件 (%1 字节) 到串口，是否继续？")
            .arg(m_fileData.size()),
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply != QMessageBox::Yes) {
        m_status->setInfo("文件发送已取消");
        return;
    }

    // 分片发送（适合大文件，如固件升级）
    const int PACKET_SIZE = 128;  // 每包 128 字节
    int totalBytes = m_fileData.size();
    int sentBytes = 0;
    int packetIndex = 0;

    m_status->setInfo(QString("开始发送文件，总大小: %1 字节").arg(totalBytes));

    while (sentBytes < totalBytes) {
        int remaining = totalBytes - sentBytes;
        int chunkSize = qMin(PACKET_SIZE, remaining);
        QByteArray chunk = m_fileData.mid(sentBytes, chunkSize);

        // 发送数据包
        serial_manager->send(Data_CMD_Text, chunk);
        sentBytes += chunkSize;
        packetIndex++;

        // 进度显示（每 100 包更新一次）
        if (packetIndex % 100 == 0 || sentBytes >= totalBytes) {
            int progress = (sentBytes * 100) / totalBytes;
            m_status->setInfo(QString("发送进度: %1% (%2/%3 字节)")
                                  .arg(progress)
                                  .arg(sentBytes)
                                  .arg(totalBytes));
        }

        // 等待发送完成
    }

    m_status->setInfo(QString("文件发送完成！共发送 %1 字节，%2 包")
                          .arg(sentBytes)
                          .arg(packetIndex));
}

void MainWindow::on_btnOpenFile_clicked()
{
    // 协议检查
    if (!ui->comboProt->currentIndex()) {
        QMessageBox::warning(
            this,
            "协议不支持",
            "LTM 协议不支持文件操作！\n请切换到「普通串口」协议后再试。"
            );
        return;
    }

    // 打开文件对话框
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "选择要发送的文件",
        QCoreApplication::applicationDirPath(),
        "所有文件 (*.*);;二进制文件 (*.bin);;Hex文件 (*.hex)"
        );

    if (fileName.isEmpty()) {
        m_status->setInfo("打开文件已取消");
        return;
    }

    // 读取文件
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(
            this,
            "文件打开失败",
            QString("无法打开文件：%1\n错误信息：%2")
                .arg(fileName)
                .arg(file.errorString())
            );
        return;
    }

    m_fileData = file.readAll();
    file.close();

    // 检查文件是否为空
    if (m_fileData.isEmpty()) {
        QMessageBox::warning(this, "文件为空", "所选文件为空文件，无法发送！");
        m_status->setInfo("文件为空: " + QFileInfo(fileName).fileName());
        return;
    }
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

/**
 * 将原始文本转换为十六进制显示格式
 * 例如: "ABC" → "41 42 43"
 */
QString MainWindow::toHexDisplay(const QString &rawText)
{
    QByteArray utf8Data = rawText.toUtf8();
    return utf8Data.toHex(' ').toUpper();
}

/**
 * 将十六进制显示格式还原为原始文本
 * 例如: "41 42 43" → "ABC"
 * 注意：不是所有十六进制字符串都能还原为有效文本
 */
QString MainWindow::fromHexDisplay(const QString &hexDisplay)
{
    QString str = hexDisplay;
    str.remove(' ');         // 移除空格
    QByteArray hexData = str.toUtf8();
    QByteArray rawData = QByteArray::fromHex(hexData);
    return QString::fromUtf8(rawData);
}

void MainWindow::appendText(const QString &text)
{
    QTextCursor cursor = ui->plainTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    ui->plainTextEdit->setTextCursor(cursor);
}

void MainWindow::updateDisplay()
{
    ui->plainTextEdit->clear();
    // 清空显示区
    if(m_hexShowMode){
        // 十六进制显示模式
        QString hexStr = m_rawReceivedData.toHex(' ').toUpper();
        // ✅ 应用换行规则
        hexStr.replace("0D 0A", "0D 0A\n");
        appendText(hexStr);
    }else{
        // 原始数据显示模式
        QString text = decodeData(m_rawReceivedData);   // 动态数据解码（UTF-8和GBK）
        appendText(text);
    }
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

static QString decodeData(const QByteArray &data) {
    QString str = QString::fromUtf8(data);
    if (str.contains(QChar::ReplacementCharacter)) {
        // 如果不是纯 UTF-8，以本地编码（Windows 下为 GBK）解码
        str = QString::fromLocal8Bit(data);
    }
    return str;
}
