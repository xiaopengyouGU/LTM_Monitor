#ifndef WINDOW_DATA_PROCESSOR_H__
#define WINDOW_DATA_PROCESSOR_H__

#include <QObject>

class ChartManager;
class SensorData;
struct pid_data_t;             //前向声明

// 这个对象负责处理数据采样线程发送的高频数据，运行在独立线程中，
// 减轻UI主线程的处理工作量，避免出现丢指令或界面卡顿

class WindowDataProcessor : public QObject
{
    Q_OBJECT
public:
    explicit WindowDataProcessor(QObject *parent = nullptr);
    void setManager(ChartManager *manager);                   //设置图表管理器
    void setPidNum(QList<pid_data_t> *pidNum);                //设置PID数据数组
public slots:  
    void do_serialDataUpdated(uint8_t data_type, const QByteArray& data); //串口接收数据更新
signals:
    void textOrCMDReceived(uint8_t type, const QString& str);   //读取到文本或命令
    void pidActualChanged(int ch, const QString& actual);      //实际值变动
private:
    ChartManager    *m_manager;
    QList<pid_data_t> *m_pidNum;
};

#endif