#include <QChart>
#include <QLineSeries>
#include <QValueAxis>
#include <QTimer>
#include <QThread>
#include <QDateTime>
#include <QtMath>
#include <limits>
#include <QFuture>
#include <QtConcurrent>

#include "chart_manager.h"
#include "chart_manager_private.h"
#include "data_storage.h"
#include "data_importer.h"
#include "data_exporter.h"
#include "chart_def.h"
#include "chart_view.h"
// ========== 私有实现类 ==========
// Private 构造函数
ChartManager::Private::Private(int channelCount, ChartManager *parent)
    : QObject(parent), m_manager(parent)
{
    //最多支持10个通道，默认支持5个通道
    if(channelCount < 1 || channelCount > MAX_CHANNEL_NUMS) 
            m_count = DEFUALT_CHANNEL_NUMS;
    else    m_count = channelCount; 

    buildChart();
    buildData();
    buildTimer();
}

ChartManager::Private::~Private()
{
    stop();                 //停止图表管理器
    // 等待并发绘图任务完成，避免访问已销毁的 m_storage 等成员
    if (m_activeWatcher) {
        m_activeWatcher->waitForFinished();
    }
    //对象的管理交给Qt，不必手动管理
    //移动到线程中的对象还是得手动析构
    delete m_importer;
    delete m_exporter;
    delete m_storage;
}

void ChartManager::Private::buildChart()
{
    m_chart = new QChart;
    m_chart->setParent(this);       //把内存的管理交给Qt
    m_timewindow = WINDOW_TIME;
    m_mode = SHOW_MODE;             //默认自动模式
    m_useAbsTime = false;
    m_endTime = 0;
    m_baseTime = 0;
    m_color = WINDOW_COLOR;         //默认为白色

    for (int i = 0; i < m_count; ++i) {
        QLineSeries* targetSeries = new QLineSeries(m_chart);  //内存的管理交给Qt
        targetSeries->setName(QString("CH%1目标值").arg(i+1));
        targetSeries->setColor(targetColors[i]);
        QPen pen = targetSeries->pen();
        pen.setWidth(2);
        targetSeries->setPen(pen);
        m_targetSeries.append(targetSeries);
        m_chart->addSeries(targetSeries);
        targetSeries->setVisible(false);

        QLineSeries* actualSeries = new QLineSeries(m_chart);
        actualSeries->setName(QString("CH%1实际值").arg(i+1));
        actualSeries->setColor(actualColors[i]);
        pen = actualSeries->pen();
        pen.setWidth(2);
        actualSeries->setPen(pen);
        m_actualSeries.append(actualSeries);
        m_chart->addSeries(actualSeries);
        actualSeries->setVisible(false);
    }

    m_xAxis = new QValueAxis(m_chart);
    m_yAxis = new QValueAxis(m_chart);
    m_xAxis->setTitleText("时间 (s)");
    m_xAxis->setTickCount(11);
    m_xAxis->setMinorTickCount(1);
    m_yAxis->setTitleText("数值");
    m_yAxis->setTickCount(4);
    m_yAxis->setMinorTickCount(1);
    m_chart->addAxis(m_xAxis, Qt::AlignBottom);
    m_chart->addAxis(m_yAxis, Qt::AlignLeft);

    for (auto series : m_targetSeries) {
        series->attachAxis(m_xAxis);
        series->attachAxis(m_yAxis);
    }
    for (auto series : m_actualSeries) {
        series->attachAxis(m_xAxis);
        series->attachAxis(m_yAxis);
    }

    m_xAxis->setRange(-m_timewindow, 0);
    m_yAxis->setRange(-5, 5);
    _setBackColor();
    m_targetSeries[0]->setVisible(true);
    m_actualSeries[0]->setVisible(true);

    connect(m_xAxis, &QValueAxis::rangeChanged, this, &Private::do_XRangeChanged);
}

void ChartManager::Private::buildData()
{
    data_thread = new QThread(this);
    m_storage = new DataStorage(DEFUALT_CHANNEL_POINTS, m_count,nullptr);
    m_importer = new DataImporter(m_storage);
    m_exporter = new DataExporter(m_storage);
    //移入线程的对象不能有parent
    m_importer->moveToThread(data_thread);
    m_exporter->moveToThread(data_thread);
    // 连接信号
    connect(this, &Private::dataImport, m_importer, &DataImporter::do_dataImport);
    connect(this, &Private::dataExport, m_exporter, &DataExporter::do_dataExport);
    connect(m_importer, &DataImporter::importFinished, this, &Private::do_importFinished);
    connect(m_exporter, &DataExporter::exportFinished, this, &Private::do_exportFinished);
}

void ChartManager::Private::buildTimer()
{
    m_period = TIMER_PERIOD;                         //默认为100ms
    m_timer = new QTimer(this);
    m_timer->stop();
    m_timer->setInterval(m_period);
    m_timer->setTimerType(Qt::CoarseTimer);         //此处没有更新周期约100ms即可，不用太准+-5%
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &Private::updateData);

    // //压力测试
    // // chart_manager.cpp 构造函数中
    // m_statsTimer = new QTimer(this);
    // m_statsTimer->setInterval(2000);
    // connect(m_statsTimer, &QTimer::timeout, this, [this]() {
    //     qDebug() << "[pressure test] tried:" << m_updateTotal
    //              << "skipped" << m_updateSkipped
    //              << "started:" << m_taskStarted;
    // m_updateTotal = m_updateSkipped = m_taskStarted = 0;
    //});
}

void ChartManager::Private::start()
{
    data_thread->start();
    m_timer->start();
}

void ChartManager::Private::stop()
{
    m_timer->stop();
    data_thread->quit();
    data_thread->wait();
}

void ChartManager::Private::setPeriod(int ms)
{
    if (ms <= 20) ms = 20;
    m_timer->setInterval(ms);
}

void ChartManager::Private::setChannelVisible(int ch, bool targetVisible, bool actualVisible)
{
    if (ch < 0 || ch >= m_count) return;
    if (targetVisible != m_targetSeries[ch]->isVisible())
        m_targetSeries[ch]->setVisible(targetVisible);
    if (actualVisible != m_actualSeries[ch]->isVisible())
        m_actualSeries[ch]->setVisible(actualVisible);
}

void ChartManager::Private::setLegendName(int ch, const QString& name)
{
    if (ch < 0 || ch >= m_count) return;
    QLineSeries *series = m_targetSeries[ch];
    if (series->name() != name)
        series->setName(name + QString("目标值"));
    series = m_actualSeries[ch];
    if (series->name() != name)
        series->setName(name + QString("实际值"));
}

void ChartManager::Private::setMode(int mode)
{
    if (mode == m_mode) return;
    m_mode = mode;
    //模式切换后，一般100ms内会更新（实时系统）
    if(m_mode == Mode_Auto)
    {
        updateTimerPeriod();   // 更新时间周期
        updateData();          // 立即刷新
    }
}

void ChartManager::Private::setAbsTime(bool isAbs)
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

void ChartManager::Private::setBackColor(int color)
{
    if(color == m_color) return;
    m_color = color;
    _setBackColor();
}

void ChartManager::Private::setWindowTime(int windowTime) // 设置自动模式窗口的长度：单位s
{
    if(windowTime < MIN_WINDOW_TIME || windowTime > MAX_WINDOW_TIME)
        windowTime = WINDOW_TIME;                        //采用默认值
    m_timewindow = windowTime;
    if(m_mode == Mode_Auto)
    {
        updateTimerPeriod();   // 更新时间周期
        updateData();          // 立即刷新
    }
}                                                        // 设置自动模式窗口的长度：单位s

void ChartManager::Private::setChartView(ChartView* chartView)                // 绑定视图对象
{
    if(!chartView)      return;     //判空
    m_chartView = chartView;
}   

void ChartManager::Private::clearShow()
{
    for (auto series : m_targetSeries) series->clear();
    for (auto series : m_actualSeries) series->clear();
}

void ChartManager::Private::stopShow()
{
    for(int i = 0; i < m_count; ++i)
    {
        m_actualSeries[i]->setVisible(false);
        m_targetSeries[i]->setVisible(false);
    }
}

int ChartManager::Private::getMode() const
{
    return m_mode;
}

QChart* ChartManager::Private::getChart() const
{
    return m_chart;
}

void ChartManager::Private::addData(int ch, float target, float actual)
{
    if (ch < 0 || ch >= m_count) return;
    m_storage->addData(ch, target, actual);  
}

void ChartManager::Private::addData(int ch, float target, float actual, qint64 timestamp)
{
    if (ch < 0 || ch >= m_count) return;
    m_storage->addData(ch, target, actual, timestamp);   
}

void ChartManager::Private::addData(const QList<ChannelData>& dataNum)                           //添加一批数据
{
    if(!dataNum.size())     return;         //空数据直接返回
    m_storage->addData(dataNum);            //添加数据
}         

void ChartManager::Private::importData(const QString& fileName)
{
    emit dataImport(fileName);
}

void ChartManager::Private::exportData(const QString& fileName, qint64 startTime, qint64 endTime)
{
    emit dataExport(fileName, startTime, endTime);
}

void ChartManager::Private::updateData()
{   // 防止并发更新
    // 并发保护：如果上一批数据更新任务尚未完成，则跳过本次触发。
    // 这可以防止用户在快速缩放/平移时产生大量并发任务，导致系统过载。
    // 连续操作时，只有最后一次触发会真正启动任务（因为中间触发时 m_isUpdating 为 true），
    // 从而保证最终显示的是最新范围的数据，同时避免中间范围的无用计算。
    // m_updateTotal++;
    // if (m_isUpdating) { m_updateSkipped++; return; }
    if(m_isUpdating)    return;
    // 1. 检查是否有新数据
    qint64 startTime, endTime;                        // 数据时间戳的范围
    qint64 viewStart, viewEnd;
    _getViewRange(viewStart, viewEnd);                // 直接获取对应模式下，所需的显示范围

    if (m_baseTime == 0) 
        m_baseTime = m_storage->getBaseTimestamp();   // 记录基准时间戳
    
    m_storage->getTimeRange(startTime, endTime);
    if (endTime == m_endTime)       return;     // 无新数据，直接返回
    m_endTime = endTime;
    // 2. 构建任务列表
    QList<SeriesTask> tasks;

    for (int ch = 0; ch < m_count; ++ch) {
        if (m_targetSeries[ch]->isVisible()) {
            tasks.append({ch, true, viewStart, viewEnd, m_useAbsTime, POINT_THRESHOLD, false});
        }
        if (m_actualSeries[ch]->isVisible()) {
            tasks.append({ch, false, viewStart, viewEnd, m_useAbsTime, POINT_THRESHOLD, false});
        }
    }
        
    // 3. 并行处理所有任务
    processTasksParallel(tasks, false);
}

void ChartManager::Private::updateAll()
{  
    // 停止定时器，避免干扰
    m_timer->stop();

    qint64 startTime, endTime;
    m_storage->getTimeRange(startTime, endTime);
    // 使用全量时间范围（0 表示全部）
    QList<SeriesTask> tasks;
    for (int ch = 0; ch < m_count; ++ch) {
        // 实际值和目标值都使用 LTTB，默认阈值设为 3000点
        tasks.append({ch, true, 0, 0, true,  LTTB_THRESHOLD, true});
        tasks.append({ch, false, 0, 0, true, LTTB_THRESHOLD, true});
    }

    processTasksParallel(tasks, true);
}

void ChartManager::Private::do_XRangeChanged(qreal min, qreal max)
{
    if (m_mode != Mode_Auto) return;
    qreal newMin = min;
    qreal newMax = max;
    bool needAdjust = false;

    if (m_useAbsTime) {
        if (min < 0) {
            newMin = 0;
            needAdjust = true;
        }
        if (newMax <= newMin) {
            newMax = newMin + 1.0;
            needAdjust = true;
        }
    } else {
        if (max > 0) {
            newMax = 0;
            needAdjust = true;
        }
        if (newMin >= newMax) {
            newMin = newMax - 1.0;
            needAdjust = true;
        }
    }

    if (needAdjust) {
        m_xAxis->blockSignals(true);
        m_xAxis->setRange(newMin, newMax);
        m_xAxis->blockSignals(false);
    }
}

void ChartManager::Private::do_exportFinished(bool success, const QString& message)
{
    emit m_manager->exportDataFinished(success, message);
}

void ChartManager::Private::do_importFinished(bool success, const QString& message)
{
    emit m_manager->importDataFinished(success, message);
}

// 辅助函数：查找可见曲线的 Y 范围
static void _findMaxMin(const QList<QLineSeries*>& lineSeries, double& minY, double& maxY, bool& hasData, int maxPoints)
{
    for (auto series : lineSeries) {
        if (!series->isVisible()) continue;
        const auto& points = series->points();
        int count = points.size();
        if (count == 0) continue;
        int startIdx = qMax(0, count - maxPoints);
        for (int i = startIdx; i < count; ++i) {
            double y = points[i].y();
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            hasData = true;
        }
    }
}

void ChartManager::Private::_adjustYAxis(int num)
{
    if (m_mode != 0) return;
    bool hasData = false;
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();

    _findMaxMin(m_targetSeries, minY, maxY, hasData, num);
    _findMaxMin(m_actualSeries, minY, maxY, hasData, num);

    if (hasData && std::isfinite(minY) && std::isfinite(maxY)) {
        double range = maxY - minY;
        double margin = range * 0.05;
        if (range < 1e-2) {
            double center = minY;
            m_yAxis->setRange(center - 0.5, center + 0.5);
        } else {
            m_yAxis->setRange(minY - margin, maxY + margin);
        }
    } else {
        m_yAxis->setRange(-5, 5);
    }
}

void ChartManager::Private::_setBackColor()
{
    if (m_color == Color_White) {
        m_chart->setBackgroundBrush(QBrush(Qt::white));
        m_xAxis->setLabelsColor(Qt::black);
        m_yAxis->setLabelsColor(Qt::black);
        m_xAxis->setLinePen(QPen(Qt::black));
        m_yAxis->setLinePen(QPen(Qt::black));
        m_xAxis->setGridLineColor(Qt::gray);
        m_yAxis->setGridLineColor(Qt::gray);
        m_chart->legend()->setLabelColor(Qt::black);
    } else {
        m_chart->setBackgroundBrush(QBrush(Qt::black));
        m_xAxis->setLabelsColor(Qt::white);
        m_yAxis->setLabelsColor(Qt::white);
        m_xAxis->setLinePen(QPen(Qt::white));
        m_yAxis->setLinePen(QPen(Qt::white));
        m_xAxis->setGridLineColor(Qt::darkGray);
        m_yAxis->setGridLineColor(Qt::darkGray);
        m_chart->legend()->setLabelColor(Qt::white);
    }
}

void ChartManager::Private::_getViewRange(qint64& viewStart, qint64& viewEnd)     //直接获取当前视图范围
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();                       //获取当前时间戳
    qint64 base = m_storage->getBaseTimestamp();                            //获取基准时间戳
    if(base == -1){                                                         //说明此时还没有接收到数据
        viewStart = now;
        viewEnd = now;
        return;                                  
    }
    if(m_mode == Mode_Auto)   // 自动模式
    {
        viewStart = now - m_timewindow * 1000;
        viewEnd = now;
    }
    else   // 手动模式
    {
        qreal minX = m_xAxis->min();
        qreal maxX = m_xAxis->max();

        if (m_useAbsTime) {
            viewStart = base + qint64(minX * 1000.0);
            viewEnd   = base + qint64(maxX * 1000.0);
        } else {
            viewStart = now + qint64(minX * 1000.0);
            viewEnd   = now + qint64(maxX * 1000.0);
        }
        // 确保时间顺序,并限制最大窗口时间
        if (viewStart > viewEnd) std::swap(viewStart, viewEnd);
        qint64 windowLen = viewEnd - viewStart;
        if (windowLen > MAX_WINDOW_MS) {
            viewStart = viewEnd - MAX_WINDOW_MS;
            // 注意：不修改 X 轴范围，保持用户当前的缩放
        }
    }
}

void ChartManager::Private::updateTimerPeriod()
{
    //自动模式与手动模式下，窗口的刷新频率都会自动调整
    qint64 viewStart, viewEnd;
    _getViewRange(viewStart, viewEnd);
    qint64 viewRange = (viewEnd - viewStart)/1000; //获取窗口范围
    int multiplier = 1;

    if (viewRange <= WINDOW_TIME_I) {           // ≤5分钟
        multiplier = PERIOD_MUL_I;
    } else if (viewRange <= WINDOW_TIME_II) {   // 5~15分钟
        multiplier = PERIOD_MUL_II;
    } else if (viewRange <= WINDOW_TIME_III) {  // 15~30分钟
        multiplier = PERIOD_MUL_III;
    } else if (viewRange <= WINDOW_TIME_VI) {   // 30~60分钟
        multiplier = PERIOD_MUL_VI;
    } else {                                    // >60分钟
        multiplier = PERIOD_MUL_VI;
    }

    int period = m_period * multiplier;           // 基础周期 * 倍数
    if (m_timer && m_timer->isActive()) {         // 下个定时周期起作用
        m_timer->setInterval(period);
    }
}

void ChartManager::Private::processTasksParallel(const QList<SeriesTask>& tasks, bool isUpdateAll)
{
    if (tasks.isEmpty()) return;
    // 标记更新开始
    //m_taskStarted++;
    m_isUpdating = true;
    QFuture<SeriesResult> future = QtConcurrent::mapped(tasks, [this](const SeriesTask& task) -> SeriesResult {
    
        // 在工作线程中调用 DataStorage 获取点列表
        QList<QPointF>* points = m_storage->getPointNum(task.channel, task.viewStart, task.viewEnd,
                                                        task.isAbs, task.isTarget, task.threshold,
                                                        task.useLTTB);
        return SeriesResult{task.channel, points, task.isTarget};
    });

    // 使用 QFutureWatcher 异步等待完成
    QFutureWatcher<SeriesResult>* watcher = new QFutureWatcher<SeriesResult>(this);
    m_activeWatcher = watcher; // 存储
    connect(watcher, &QFutureWatcher<SeriesResult>::finished, this, [this, watcher, isUpdateAll]() {
        QList<SeriesResult> results = watcher->future().results();
        onAllResultsReady(results);
        watcher->deleteLater();
        m_activeWatcher = nullptr;
        if (isUpdateAll) {
            // 如果是 updateAll，还需要恢复定时器等操作
            finishUpdateAll();
        }
    });
    watcher->setFuture(future);
}

void ChartManager::Private::onAllResultsReady(const QList<SeriesResult>& results)
{
    // 禁用视图更新，避免中间状态重绘
    if (m_chartView) m_chartView->setUpdatesEnabled(false);

    // 更新所有曲线
    for (const auto& result : results) {
        if(!result.points)      continue;         //判空
        QLineSeries* series = result.isTarget ? m_targetSeries[result.channel] : m_actualSeries[result.channel];
        series->replace(*result.points);
    }
    
    // 更新坐标轴（根据模式）
    if (m_mode == Mode_Auto) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 base = m_storage->getBaseTimestamp();
        if (m_useAbsTime) {
            qreal minX = (now - m_timewindow * 1000.0 - base) / 1000.0;
            qreal maxX = (now - base) / 1000.0;
            m_xAxis->setRange(minX, maxX);
        } else {
            m_xAxis->setRange(-m_timewindow, 0);
        }
        _adjustYAxis(POINT_THRESHOLD);
    }
    // 手动模式下保持用户缩放，无需自动调整 X 轴
    // 但需要限制最大窗口（已在 flushChartView 中处理，这里可省略）
    // 重新启用视图更新并强制刷新
    if (m_chartView) {
        m_chartView->setUpdatesEnabled(true);
        m_chartView->update();
    }
    // 所有更新完成，重置标志
    m_isUpdating = false;
}

void ChartManager::Private::finishUpdateAll()
{   // 重新启动定时器
    if (m_timer->isActive()) m_timer->start(); // 如果之前是启动状态
}

// ========== ChartManager 公共接口实现 ==========
ChartManager::ChartManager(int channelCount, QObject *parent) : QObject(parent), 
    pimpl(new Private(channelCount, this)){}
ChartManager::~ChartManager(){};                //Qt负责内存管理

void ChartManager::start()                      { pimpl->start();}
void ChartManager::stop()                       { pimpl->stop(); }
void ChartManager::setPeriod(int ms)            { pimpl->setPeriod(ms); }
void ChartManager::setChannelVisible(int channel, bool targetVisible, bool actualVisible) { pimpl->setChannelVisible(channel, targetVisible, actualVisible); }
void ChartManager::setLegendName(int channel, const QString& name) { pimpl->setLegendName(channel, name); }
void ChartManager::setMode(int mode)            { pimpl->setMode(mode); }
void ChartManager::setAbsTime(bool isAbs)       { pimpl->setAbsTime(isAbs); }
void ChartManager::setBackColor(int color)      { pimpl->setBackColor(color); }
void ChartManager::setChartView(ChartView *chartView)   { pimpl->setChartView(chartView); }
void ChartManager::setWindowTime(int windowTime){ pimpl->setWindowTime(windowTime); }
void ChartManager::clearShow()                  { pimpl->clearShow(); }
void ChartManager::stopShow()                   { pimpl->stopShow(); }
int ChartManager::getMode() const               { return pimpl->getMode(); }
QChart* ChartManager::getChart() const          { return pimpl->getChart(); }
void ChartManager::addData(int ch, float target, float actual) { pimpl->addData(ch, target, actual); }
void ChartManager::addData(int ch, float target, float actual, qint64 timestamp) { pimpl->addData(ch, target, actual, timestamp);}
void ChartManager::addData(const QList<ChannelData>& dataNum) { pimpl->addData(dataNum);}
void ChartManager::importData(const QString& fileName) { pimpl->importData(fileName); }
void ChartManager::exportData(const QString& fileName, qint64 startTime, qint64 endTime) { pimpl->exportData(fileName, startTime, endTime); }
void ChartManager::updateData()                 { pimpl->updateData(); }
void ChartManager::updateAll()                  { pimpl->updateAll(); }
