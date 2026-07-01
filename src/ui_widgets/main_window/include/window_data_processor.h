#ifndef WINDOW_DATA_PROCESSOR_H__
#define WINDOW_DATA_PROCESSOR_H__

#include <QObject>
#include <QRegularExpression>
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
    void textOrCMDReceived(uint8_t type, const QString& str, const QByteArray& data);   //读取到文本或命令
    void pidActualChanged(int ch, const QString& actual);       //实际值变动
    void pidActualChanged(const QStringList& actNum);           //实际值变动
private:
    QString data2Str(float value);                      //将数据转换为String,动态显示小数位

    static constexpr int CURVES_SIZE = 5;               //支持的曲线数量
    static constexpr int MIN_PERIOD = 47;               //至少间隔 47ms 发送一次数据到 UI主线程
    QRegularExpression trailingZeros;      //正则表达式
    QRegularExpression trailingDot;
private:
    ChartManager    *m_manager;
    QList<pid_data_t> *m_pidNum;

};

#endif
