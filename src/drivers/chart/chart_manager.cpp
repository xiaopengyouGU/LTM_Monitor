#include "chart_manager.h"
#include <QtMath>
#include <QDateTime>

ChartManager::ChartManager(QChart* chart):m_chart(chart),m_timewindow(30.0),m_mode(0),m_useAbsTime(false)
{
    Q_ASSERT_X(m_chart, "ChartManager", "chart cannot be nullptr");
    m_endTime = 0;
    m_baseTime = 0;             //未设置基准时间戳
    m_color = 0;                //默认背景色为白色
    // 定义目标值和实际值的颜色数组（5通道）
    const QColor targetColors[5] = {
        Qt::red, QColor(255, 128, 0), Qt::magenta, Qt::blue, Qt::darkBlue
    };
    const QColor actualColors[5] = {
        Qt::green, Qt::yellow, Qt::darkGreen, Qt::cyan, Qt::darkYellow
    };

    // 初始化5个通道的目标值和实际值序列
    for(int i = 0; i < 5; ++i) 
    {
        QLineSeries* targetSeries = new QLineSeries();
        targetSeries->setName(QString("CH%1目标值").arg(i+1));
        targetSeries->setColor(targetColors[i]);          // 设置目标值颜色
         // 设置线条宽度，例如 2
        QPen pen = targetSeries->pen();
        pen.setWidth(2);
        targetSeries->setPen(pen);
        m_targetSeries.append(targetSeries);
        m_chart->addSeries(targetSeries);
        targetSeries->setVisible(false);
        //targetSeries->setUseOpenGL(true);                //采用OpenGL加速,不推荐

        QLineSeries* actualSeries = new QLineSeries();
        actualSeries->setName(QString("CH%1实际值").arg(i+1));
        actualSeries->setColor(actualColors[i]);          // 设置实际值颜色
        pen = actualSeries->pen();
        pen.setWidth(2);
        actualSeries->setPen(pen);
        m_actualSeries.append(actualSeries);
        m_chart->addSeries(actualSeries);
        actualSeries->setVisible(false);
        //actualSeries->setUseOpenGL(true);                //采用OpenGL加速，不推荐
    }

    // 创建并设置坐标轴
    m_xAxis = new QValueAxis;
    m_yAxis = new QValueAxis;
    m_xAxis->setTitleText("时间 (s)");
    m_xAxis->setTickCount(11);        //主刻度数
    m_xAxis->setMinorTickCount(1);    //次刻度数
    m_yAxis->setTitleText("数值");
    m_yAxis->setTickCount(5);        //主刻度数
    m_yAxis->setMinorTickCount(1);    //次刻度数
    m_chart->addAxis(m_xAxis, Qt::AlignBottom);
    m_chart->addAxis(m_yAxis, Qt::AlignLeft);

    // 将所有序列附加到坐标轴
    for (auto series : m_targetSeries) {
        series->attachAxis(m_xAxis);
        series->attachAxis(m_yAxis);
    }
    for (auto series : m_actualSeries) {
        series->attachAxis(m_xAxis);
        series->attachAxis(m_yAxis);
    }

    // 初始设置默认范围
    m_xAxis->setRange(-m_timewindow, 0);   // 显示最近20秒
    m_yAxis->setRange(-5, 5);
    _setBackColor();             // 设置背景颜色
    m_targetSeries[0]->setVisible(true);
    m_actualSeries[0]->setVisible(true);
    //避免X轴缩放过程中，出现不该出现的值
    connect(m_xAxis, &QValueAxis::rangeChanged, this, &ChartManager::do_XRangeChanged);
}

ChartManager::~ChartManager()
{
    for(int i = 0; i < 5; i++)
    {
        m_chart->removeSeries(m_actualSeries[i]);
        m_chart->removeSeries(m_targetSeries[i]);
        delete m_actualSeries[i];
        delete m_targetSeries[i];
    }
    m_chart->removeAxis(m_xAxis);
    m_chart->removeAxis(m_yAxis);
    delete m_xAxis;
    delete m_yAxis;
}

void ChartManager::setChannelVisible(int ch, bool targetVisible, bool actualVisible)
{
    if(ch < 0 || ch >= 5) return;                    //检查通道是否有效

    if(targetVisible != m_targetSeries[ch]->isVisible())
        m_targetSeries[ch]->setVisible(targetVisible);
    if(actualVisible != m_actualSeries[ch]->isVisible())
        m_actualSeries[ch]->setVisible(actualVisible);
}

void ChartManager::setLegendName(int ch, bool isTarget, const QString& name)
{
    if(ch < 0 || ch >= 5) return;                    //检查通道是否有效

    QLineSeries *series = isTarget ? m_targetSeries[ch] : m_actualSeries[ch];
    if(series->name() != name)
        series->setName(name);
}   

void ChartManager::setMode(int mode)
{
    if(mode == m_mode) return;                      //不需要更改模式            
    m_mode = mode;                                  //自动模式
}

void ChartManager::setAbsTime(bool isAbs)
{
    if(isAbs == m_useAbsTime)   return;

    m_useAbsTime = isAbs;
    // 转换X轴范围以保持相同的数据区间
    qreal minX = m_xAxis->min();
    qreal maxX = m_xAxis->max();
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    //得有数据的情况下，才会调用更新m_baseTime，否则接口是无效的
    if(isAbs) 
    {   // 从相对时间切换到绝对时间
        // 相对时间：X = (时间戳 - now) / 1000
        qint64 startTime = now + qint64(minX * 1000.0);
        qint64 endTime   = now + qint64(maxX * 1000.0);
        if (startTime > endTime) std::swap(startTime, endTime);
        qreal newMinX = (startTime - m_baseTime) / 1000.0;
        qreal newMaxX = (endTime   - m_baseTime) / 1000.0;
        m_xAxis->setRange(newMinX, newMaxX);
    } 
    else 
    {  // 从绝对时间切换到相对时间
        // 绝对时间：X = (时间戳 - base) / 1000
        qint64 startTime = m_baseTime + qint64(minX * 1000.0);
        qint64 endTime   = m_baseTime + qint64(maxX * 1000.0);
        if (startTime > endTime) std::swap(startTime, endTime);
        qreal newMinX = (startTime - now) / 1000.0;
        qreal newMaxX = (endTime   - now) / 1000.0;
        m_xAxis->setRange(newMinX, newMaxX);
    }
}

void ChartManager::setBackColor(int color)
{
    if(color == m_color)    return;                 //不需要改动
    m_color = color;  
    _setBackColor();                                //设置背景颜色
}

void ChartManager::updateData(DataStorage* storage)
{
    qint64 startTime, endTime;
    if(m_baseTime == 0) m_baseTime = storage->getBaseTimeStamp();   //记录基准时间戳
    storage->getTimeRange(startTime, endTime);
    if(endTime == m_endTime) return;             // 无新数据
    m_endTime = endTime;


    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 base = storage->getBaseTimeStamp();    

    if(m_mode == 0)   // 自动模式
    {
        for(int ch = 0; ch < 5; ++ch) {
            storage->fillSeries(ch, (now - m_timewindow*1000), now,
                                m_targetSeries[ch], m_actualSeries[ch], m_useAbsTime);
        }

        if(m_useAbsTime) 
        {   // 绝对时间轴：X轴显示 [ (now - 窗口) - base, now - base ] 秒
            qreal minX = (now - m_timewindow * 1000.0 - base) / 1000.0;
            qreal maxX = (now - base) / 1000.0;
            m_xAxis->setRange(minX, maxX);
        } 
        else 
        {   // 相对时间轴：固定显示最近 m_timewindow 秒
            m_xAxis->setRange(-m_timewindow, 0);
        }
        _adjustYAxis();   // Y轴自动缩放
    }
    else if(m_mode == 1)   // 手动模式
    {
        // 根据当前X轴范围和模式计算出对应的时间戳区间
        qreal minX = m_xAxis->min();
        qreal maxX = m_xAxis->max();
        qint64 viewStart, viewEnd;

        if(m_useAbsTime) 
        {
            // 绝对时间：X数值 = (时间戳 - base) / 1000
            viewStart = base + qint64(minX * 1000.0);
            viewEnd   = base + qint64(maxX * 1000.0);
        } 
        else 
        {
            // 相对时间：X数值 = (时间戳 - now) / 1000
            viewStart = now + qint64(minX * 1000.0);
            viewEnd   = now + qint64(maxX * 1000.0);
        }
        // 确保时间顺序
        if (viewStart > viewEnd) std::swap(viewStart, viewEnd);
        // 限制最大窗口为60分钟，避免数据量过大
        const qint64 MAX_WINDOW_MS = 60 * 60 * 1000;
        qint64 windowLen = viewEnd - viewStart;
        if (windowLen > MAX_WINDOW_MS) {
            // 截断为最近60分钟（保留较新的部分）
            viewStart = viewEnd - MAX_WINDOW_MS;
            // 同步更新X轴范围，使其与实际加载的数据一致
            if(m_useAbsTime)
                m_xAxis->setRange((viewStart - base) / 1000.0, (viewEnd - base) / 1000.0);
            else
                m_xAxis->setRange((viewStart - now) / 1000.0, (viewEnd - now) / 1000.0);
        }

        const int LTTB_THRESHOLD = 2000;   // 每条曲线最多显示2000点
        for (int ch = 0; ch < 5; ++ch) 
            storage->fillSeriesLTTB(ch, viewStart, viewEnd,
                                    m_targetSeries[ch], m_actualSeries[ch],
                                    m_useAbsTime, LTTB_THRESHOLD);
        // 手动模式下不调整Y轴
    }

    m_lastNow = now;   // 记录本次更新时间（如果需要用于其他目的）
}

//该函数取所有数据, 以绝对时间显示。
void ChartManager::updateAll(DataStorage* storage)
{
    qint64 startTime, endTime;
    bool hasData;
    double minY, maxY;
    storage->getTimeRange(startTime, endTime);
    for(int ch = 0; ch < 5; ch++)
    {
        storage->fillSeriesLTTB(ch, 0, 0, m_targetSeries[ch], m_actualSeries[ch], true,2000);
    } 
    m_xAxis->setRange(0, (endTime - startTime)/1000.0);
    _adjustYAxis(2000);                     //Y轴自动缩放
}

void ChartManager::do_XRangeChanged(qreal min, qreal max)
{
    if (m_mode != 1) return;      // 只在手动模式下限制

    qreal newMin = min;
    qreal newMax = max;
    bool needAdjust = false;

    if (m_useAbsTime) 
    {
        if (min < 0)
        {   // 绝对时间模式：X 轴数值必须 >= 0 
            newMin = 0;
            needAdjust = true;
        }
        if (newMax <= newMin) 
        {   // 避免范围倒置或为零宽度
            newMax = newMin + 1.0;   // 至少 1 秒宽度
            needAdjust = true;
        }
    }
    else 
    {
        if (max > 0) 
        {   // 相对时间模式：X 轴数值必须 <= 0
            newMax = 0;
            needAdjust = true;
        }
        if (newMin >= newMax) 
        {   // 避免范围倒置或为零宽度
            newMin = newMax - 1.0;   // 至少 1 秒宽度（负数区间）
            needAdjust = true;
        }
    }

    if(needAdjust) 
    {
        // 阻塞信号，防止循环触发
        m_xAxis->blockSignals(true);
        m_xAxis->setRange(newMin, newMax);
        m_xAxis->blockSignals(false);
    }
}

/****************************************************************************/
/* 私有函数 */

static void _findMaxMin(const QList<QLineSeries*>& lineSeries, double& minY, double& maxY, bool& hasData, int maxPoints = 2000)
{
    for (auto series : lineSeries) {
        if (!series->isVisible()) continue;
        const auto& points = series->points();  // 已经是引用，无拷贝
        int count = points.size();
        if (count == 0) continue;
        // 只考虑最新的 maxPoints 个点
        int startIdx = qMax(0, count - maxPoints);
        for (int i = startIdx; i < count; ++i) {
            double y = points[i].y();
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            hasData = true;
        }
    }
}

void ChartManager::_adjustYAxis(int num)
{
    if (m_mode != 0) return;
    bool hasData = false;
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();

    // 默认遍历最近800个点（约8秒数据，假设10ms采样率）
    _findMaxMin(m_targetSeries, minY, maxY, hasData, num);
    _findMaxMin(m_actualSeries, minY, maxY, hasData, num);

    if (hasData) 
    {
        double range = maxY - minY;
        double margin = range * 0.05;
        if (range < 1e-2) 
        {
            double center = minY;
            m_yAxis->setRange(center - 0.5, center + 0.5);
        } 
        else    m_yAxis->setRange(minY - margin, maxY + margin);
    } 
    else    m_yAxis->setRange(-5, 5);
}

void ChartManager::_setBackColor()
{
    if(m_color == 0) 
    {   // 白色背景
       m_chart->setBackgroundBrush(QBrush(Qt::white));
       m_xAxis->setLabelsColor(Qt::black);
       m_yAxis->setLabelsColor(Qt::black);
       m_xAxis->setLinePen(QPen(Qt::black));
       m_yAxis->setLinePen(QPen(Qt::black));
       m_xAxis->setGridLineColor(Qt::gray);
       m_yAxis->setGridLineColor(Qt::gray);
       //m_chart->setTitleBrush(QBrush(Qt::black));
       m_chart->legend()->setLabelColor(Qt::black);
    } 
    else 
    {            // 黑色背景
        m_chart->setBackgroundBrush(QBrush(Qt::black));
        m_xAxis->setLabelsColor(Qt::white);
        m_yAxis->setLabelsColor(Qt::white);
        m_xAxis->setLinePen(QPen(Qt::white));
        m_yAxis->setLinePen(QPen(Qt::white));
        m_xAxis->setGridLineColor(Qt::darkGray);
        m_yAxis->setGridLineColor(Qt::darkGray);
        //m_chart->setTitleBrush(QBrush(Qt::white));
        m_chart->legend()->setLabelColor(Qt::white);
    }
}