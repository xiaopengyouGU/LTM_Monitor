#include "window_data_processor.h"
#include "mainwindow.h"
#include "chart.h"
#include "serial.h"

#include <cstring>

WindowDataProcessor::WindowDataProcessor(QObject *parent):QObject(parent)
{
    static const bool registered = []() {
        qRegisterMetaType<CanfdFrameRow>("CanfdFrameRow");
        qRegisterMetaType<QList<CanfdFrameRow>>("QList<CanfdFrameRow>");
        return true;
    }();
    Q_UNUSED(registered);

    m_manager = nullptr;
    m_pidNum = nullptr;
    trailingZeros = QRegularExpression("0+$");
    trailingDot   = QRegularExpression("\\.$");

    // CAN-FD 排水定时器（与槽函数同在 process_thread 中运行）
    m_canDrainTimer = new QTimer(this);
    m_canDrainTimer->setInterval(CANFD_DRAIN_MS);
    m_canDrainTimer->setSingleShot(false);
    m_canDrainTimer->stop();
    connect(m_canDrainTimer, &QTimer::timeout, this, &WindowDataProcessor::do_canfdDrain);
}

void WindowDataProcessor::setManager(ChartManager *manager)                     //设置图表管理器
{
    if(!manager)        return;
    m_manager = manager;
}

void WindowDataProcessor::setPidNum(QList<pid_data_t> *num)                   //设置轴数据数组
{
    if(!num)            return;
    m_pidNum = num;
}

QString WindowDataProcessor::data2Str(float value)      //将数据转换为String,动态显示小数位
{
    int pos = 3;
    float abs_val = qAbs(value);                        //动态显示小数位
    if (abs_val > 10000)     pos = 0;                   //上万，不显示小数
    else if (abs_val > 1000) pos = 1;                   //上千，显示1位
    else if (abs_val > 30)   pos = 2;                   //几十到几百，显示2位
    else pos = 3;                                       //小于30，显示3位
    QString str = QString::number(value, 'f', pos);     //先固定3位小数,最多显示小数点后三位
    str.remove(trailingZeros).remove(trailingDot);      //去除末尾零及可能的小数点
    return str;
}

void WindowDataProcessor::do_serialDataUpdated(uint8_t type, const QByteArray& data) //串口接收数据更新
{
    if(!m_manager || !m_pidNum)         return;         //未设置管理器或PID数组，直接返回
    //接收到数据了,根据接收到的数据类型直接进行处理（暂时）
    switch(type)
    {
        case Data_Channel_ALL:                          //接收到通道数据
        {
            float values[CURVES_SIZE];
            memcpy(values, data.constData(), data.size());
            static qint64 ts_last = QDateTime::currentMSecsSinceEpoch();  //上次发送处理后数据到 UI 主线程
            qint64 ts_now = QDateTime::currentMSecsSinceEpoch(); //获取当前时间戳
            int cnt = (int)data.size() >> 2;                  //除以4字节（float）
            cnt = qMin(cnt, CURVES_SIZE);
            QList<ChannelData> dataNum(cnt);

            for(int i = 0; i < cnt; i++){
                float target = m_pidNum->at(i).target;     //此处的ch取值 0 - 4 开始的，实际对应通道CH1——CH5
                (*m_pidNum)[i].actual = values[i];         //记录实际值
                dataNum[i].timestamp.append(ts_now);       //时间戳相同
                dataNum[i].actual.append(values[i]);       //添加实际值
                dataNum[i].target.append(target);          //添加目标值
            }
            m_manager->addData(dataNum);                   //批量添加，更高效

            // 判断发送给 UI 主线的时间间隔
            if((ts_now - ts_last) >= MIN_PERIOD){
                ts_last = ts_now;                           //更新ts_last
                QStringList strNum;
                for(int i = 0; i < cnt; i++){
                    float actual = values[i];
                    strNum << data2Str(actual);
                }
                emit pidActualChanged(strNum);              //发送数据到 UI 主线程
            }
            break;
        }
        case Data_Channel1:
        case Data_Channel2:
        case Data_Channel3:
        case Data_Channel4:
        case Data_Channel5:
        {
            int ch = type - Data_Channel1;  //通道枚举值是连续递增的
            if(ch >= m_pidNum->size())   return;         //通道序号不合理
            static int count[CURVES_SIZE] = {0};         //高频通道数据接收时，降低更新实际值UI的频率

            float actual;
            memcpy(&actual, data.constData(), data.size());
            float target = m_pidNum->at(ch).target;      //此处的ch取值 0 - 4 开始的，实际对应通道CH1——CH5
            (*m_pidNum)[ch].actual = actual;             //记录实际值
            m_manager->addData(ch, target, actual);      //数据存储

            if(count[ch]++ % 3 == 0){
                QString str = QString::number(actual, 'f', 3);      // 先固定3位小数,最多显示小数点后三位
                str.remove(trailingZeros).remove(trailingDot);      // 去除末尾零及可能的小数点
                emit pidActualChanged(ch, str);                     // 利用Qt的隐式共享机制
            }
            break;
        }
        case Data_CMD_Text:
        {
            QString str = QString::fromUtf8(data);
            emit textOrCMDReceived(type, str, data);
            //ui->plainTextEdit->appendPlainText(str);
            break;
        }
        case Data_Res_Start:                        //下位机响应启动
        {
            emit textOrCMDReceived(type, "下位机启动成功",data);
            break;
        }
        case Data_Res_Stop:                         //下位机响应停止
        {
            emit textOrCMDReceived(type, "下位机停机成功",data);
            break;
        }
        default : break;

    }

}

/********************************** CAN-FD 中转 ************************************/
bool WindowDataProcessor::loadCanfdProtocol(const QString &filePath, QString *error)
{
    if (filePath.endsWith(".dbc", Qt::CaseInsensitive))
        return m_dataMap.loadDbc(filePath, error);
    return m_dataMap.loadJson(filePath, error);
}

void WindowDataProcessor::clearCanfd()
{
    m_canBuf.clear();
    m_canDrainTimer->stop();
    std::memset(m_holdValid, 0, sizeof(m_holdValid));
}

void WindowDataProcessor::do_canfdDataUpdated(const QList<CanfdFrame> &frames)
{
    // 中转站职责：把原始帧解析为表格行（格式化在源头做一次），转发给表格
    QList<CanfdFrameRow> rows;
    rows.reserve(frames.size());
    for (const CanfdFrame &f : frames)
        rows.append(CanfdFrameRow::fromFrame(f, false));
    emit canfdRowsReceived(rows);
    int dropped = 0;
    for (const CanfdFrame &f : frames) {
        if (m_canBuf.size() >= CANFD_BUF_MAX) {
            dropped++;               // 缓冲已满，丢弃剩余新帧
            continue;
        }
        m_canBuf.append(f);
    }
    if (dropped > 0) {
        m_canfdDropCount += dropped;
        emit canfdDropped(dropped, m_canfdDropCount);
    }
    if (m_canBuf.size() >= CANFD_DRAIN_THRESHOLD)
        do_canfdDrain();             // 突发：达到阈值立即排水
    else if (!m_canDrainTimer->isActive())
        m_canDrainTimer->start();    // 涓流：定时器兜底
}

void WindowDataProcessor::do_canfdFramesSent(const QList<CanfdFrame> &frames)
{
    // 与接收同源：帧 -> 表格行（格式化在源头做一次），发给表格
    QList<CanfdFrameRow> rows;
    rows.reserve(frames.size());
    for (const CanfdFrame &f : frames)
        rows.append(CanfdFrameRow::fromFrame(f, true));
    emit canfdRowsSent(rows);
}

void WindowDataProcessor::do_canfdDrain()
{
    if (m_canBuf.isEmpty()) {
        m_canDrainTimer->stop();
        return;
    }

    QVector<CanfdFrame> frames;
    frames.swap(m_canBuf);           // 整批取出，避免逐帧拷贝
    if (m_canBuf.isEmpty())
        m_canDrainTimer->stop();

    for (int i = 0; i < frames.size(); i++) {
        const CanfdFrame &frame = frames[i];
        QList<DataMapDecodedSignal> sigs;
        QString err;
        qint64 arrivalMs = frame.timestampEpochMs > 0 ? frame.timestampEpochMs : QDateTime::currentMSecsSinceEpoch();
        if (m_dataMap.decode(frame.id, frame.isEff(), frame.data, sigs, &err))
            drainToChart(sigs, arrivalMs);
        // 未定义的 ID 直接忽略，保持通道上一个有效值
    }
}

void WindowDataProcessor::drainToChart(const QList<DataMapDecodedSignal> &sigs, qint64 arrivalMs)
{
    if (!m_manager)     return;

    // 先更新各通道的采样保持值
    for (const DataMapDecodedSignal &s : sigs) {
        if (s.chartChannel < 1 || s.chartChannel > CURVES_SIZE)
            continue;                // 0 = 不映射（仅表格/日志）
        int ch = s.chartChannel - 1;
        int idx = s.isTarget ? 0 : 1;
        m_holdValue[ch][idx] = s.value;
        m_holdValid[ch][idx] = true;
    }

    // 成对写入：另一半未更新的信号保持上次值（sample-and-hold）
    for (int ch = 0; ch < CURVES_SIZE; ch++) {
        if (!m_holdValid[ch][0] && !m_holdValid[ch][1])
            continue;
        m_manager->addData(ch, m_holdValue[ch][0], m_holdValue[ch][1], arrivalMs);
    }
}
