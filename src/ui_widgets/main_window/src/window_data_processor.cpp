#include "window_data_processor.h"
#include "mainwindow.h"
#include "chart.h"
#include "serial.h"

WindowDataProcessor::WindowDataProcessor(QObject *parent):QObject(parent)
{
    m_manager = nullptr;
    m_pidNum = nullptr;
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

void WindowDataProcessor::do_serialDataUpdated(uint8_t type, const QByteArray& data) //串口接收数据更新
{
    if(!m_manager || !m_pidNum)         return;         //未设置管理器或PID数组，直接返回
    //接收到数据了,根据接收到的数据类型直接进行处理（暂时）
    switch(type)
    {
        case Data_CMD_Text:
        {
            QString str = QString::fromUtf8(data);
            emit textOrCMDReceived(type, str);
            //ui->plainTextEdit->appendPlainText(str);
            break;
        }   //接收到通道数据
        case Data_Channel1:
        case Data_Channel2:
        case Data_Channel3:
        case Data_Channel4:
        case Data_Channel5:
        {
            int ch = type - Data_Channel1;  //通道枚举值是连续递增的
            if(ch >= m_pidNum->size())   return;         //通道序号不合理
            static int count[5] = {0};                   //高频通道数据接收时，降低更新实际值UI的频率

            float actual;
            memcpy(&actual, data.constData(), data.size());
            float target = m_pidNum->at(ch).target;     //此处的ch取值 0 - 4 开始的，实际对应通道CH1——CH5
            (*m_pidNum)[ch].actual = actual;            //记录实际值      
            m_manager->addData(ch, target, actual);     //数据存储
            
            if(count[ch]++ % 3 == 0){
                static const QRegularExpression trailingZeros("0+$");
                static const QRegularExpression trailingDot("\\.$");
                QString str = QString::number(actual, 'f', 3);      // 先固定3位小数,最多显示小数点后三位
                str.remove(trailingZeros).remove(trailingDot);      // 去除末尾零及可能的小数点
                emit pidActualChanged(ch, str);                     // 利用Qt的隐式共享机制
            }
            break;
        }
        case Data_Res_Start:                        //下位机响应启动
        {
            emit textOrCMDReceived(type, "下位机启动成功");
            break;
        }
        case Data_Res_Stop:                         //下位机响应停止
        {
            emit textOrCMDReceived(type, "下位机停机成功");
            break;
        }
        default : break;

    }

}
