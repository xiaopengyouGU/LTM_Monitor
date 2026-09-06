#include "chart_controller.h"
#include "qcustomplot.h"
#include <QMouseEvent>
#include <limits>

// rangeChanged 有重载（单参/双参），统一强转单参版
using RangeChangedSig = void (QCPAxis::*)(const QCPRange&);

// ChartController：渲染层适配器。
// QCustomPlot 仅在本文件中可见，头文件对外只暴露 QWidget*，换渲染引擎只动本文件。
// 与 ChartManager 同款结构：Public 接口只做薄委托，真实逻辑全部在 Private 中。
class ChartController::Private {
public:
    explicit Private(ChartController *owner, QWidget *parent);
    ~Private();

    QWidget* getWidget() const;
    void setViewType(ViewType type);
    void clear();
    void clearData();
    void updateData(int channel, const QList<double>& x, const QList<double>& y);
    void updateSpectrum(const QList<double>& freq, const QList<double>& mag);
    void addChannel(int channel, const QString& name = QString(), const QColor& color = Qt::red);
    void removeChannel(int channel);
    void setChannelVisible(int channel, bool visible);
    void setViewVisible(bool visible);
    void rescaleYToWindow();
    void setChannelName(int channel, const QString& name);
    void setChannelColor(int channel, const QColor& color);
    void setChannels(const QList<int>& channels);
    void setXYChannels(const QList<int>& channels);
    void setXRange(double min, double max);
    void setYRange(double min, double max);
    void setXAxisTitle(const QString& title);
    void setYAxisTitle(const QString& title);
    void setBackColor(const QColor& color);
    void setViewRange(double min, double max);
    void setAbsTime(bool enabled);
    void setAutoFollow(bool enabled);
    ViewType viewType() const;
    QList<int> channels() const;
    bool absTime() const;
    bool autoFollow() const;
    QPair<double,double> viewRange() const;
    void setInteraction(bool drag, bool zoom);
    void setRangeDragAxes(bool x, bool y);
    void setRangeZoomFactor(double xFactor, double yFactor);
    void replot();
    void replotQueued();

private:
    ChartController *m_owner = nullptr;
    QCustomPlot* plot = nullptr;
    ViewType     type = View_Waveform;
    QHash<int, QCPGraph*> graphs;          // 通道号 -> 波形曲线
    QCPGraph*    spectrumGraph = nullptr;  // 频谱曲线（WaveformSpectrum 下半区）
    QCPGraph*    xyGraph  = nullptr;       // XY 曲线
    QCPAxisRect* specRect = nullptr;       // 频谱轴矩形
    QList<int>   xyChannels;               // XY：前两个 = A(X)、B(Y)
    QList<double> xyX;                     // A 通道数值（作 X）
    QList<double> xyY;                     // B 通道数值（作 Y）
    QList<int>   m_channels;               // 视图绑定的通道列表（单一数据源，含 XY/频谱）
    bool         m_absTime = false;        // true=绝对时间，false=相对时间
    bool         m_autoFollow = true;      // 自动跟随最新（用户拖拽/设范围后置 false）
    QPair<double,double> m_viewRange = {0.0, 0.0};   // 视图显示范围（手动模式时有效）
    bool         m_fullReplotPending = true;          // true=需要整图重绘（首帧/轴范围/外观/布局变化）
};

// ============================================================
// Private：真实逻辑
// ============================================================
ChartController::Private::Private(ChartController *owner, QWidget *parent)
    : m_owner(owner)
{
    plot = new QCustomPlot(parent);
    // 实时曲线关抗锯齿：折线光栅化开销大降；文字/网格仍走各自的 AA 提示
    plot->setNotAntialiasedElements(QCP::aePlottables);
    // =====================================================================
    // ★★★ 关键性能修复：实时刷新卡顿的根因就在这行，动它之前务必先读下面说明 ★★★
    // ---------------------------------------------------------------------
    // QCustomPlot 默认用 QPainter::drawPolyline 画曲线。Qt6 在 Windows 上对
    // "非抗锯齿 + 线宽>1" 的折线光栅化存在病态慢路径：本项目实测仅 3816 点、
    // 882x520 视口，单帧重绘就要 ~870ms（表现为"曲线波动越大越卡"）。
    //
    // phFastPolylines 让 QCustomPlot 改用逐段 QPainter::drawLine 绘制折线，
    // 单帧重绘从 ~870ms 降到 ~10ms（约 87 倍）。代价只是折线端点/线帽的
    // 抗锯齿细节略降，实时滚动场景完全无感。
    //
    // 警告：这行不要删、不要挪、不要"顺手优化"成其它写法（如改回 drawPolyline
    // 或移除曲线线宽），否则实时刷新卡顿会立刻复现。
    // =====================================================================
    plot->setPlottingHint(QCP::phFastPolylines);

    // 鼠标移动：上报坐标（高频，调用方自行节流）
    connect(plot, &QCustomPlot::mouseMove, owner, [this](QMouseEvent *e) {
        QCPAxis *x = plot->xAxis;
        QCPAxis *y = plot->yAxis;
        emit m_owner->mouseMove(x->pixelToCoord(e->pos().x()), y->pixelToCoord(e->pos().y()));
    });
    // X 轴范围变化（用户拖拽/缩放）：更新视图状态并通知 ChartManager
    connect(plot->xAxis, static_cast<RangeChangedSig>(&QCPAxis::rangeChanged),
            owner, [this](const QCPRange &r) {
                m_viewRange = QPair<double,double>(r.lower, r.upper);
                m_autoFollow = false;                    // 用户拖拽/缩放 -> 切手动
                emit m_owner->rangeChanged(r.lower, r.upper);
            });
}

ChartController::Private::~Private() = default;

QWidget* ChartController::Private::getWidget() const
{
    return plot;
}

// 切换视图类型：曲线全部重建，通道绑定清空（调用方需重新 setChannels）
void ChartController::Private::setViewType(ViewType type)
{
    clear();
    this->type = type;
    m_fullReplotPending = true;            // 结构重建：下一帧整图重绘

    // 频谱下半区：需要时在默认轴矩形（波形区）下方插入一行
    if (type == View_WaveformSpectrum) {
        specRect = new QCPAxisRect(plot);
        plot->plotLayout()->insertRow(1);
        plot->plotLayout()->addElement(1, 0, specRect);

        spectrumGraph = plot->addGraph(
            specRect->axis(QCPAxis::atBottom),
            specRect->axis(QCPAxis::atLeft));
        spectrumGraph->setAdaptiveSampling(true);    // 实时重绘按像素抽点，进一步降低绘制量
        spectrumGraph->setPen(QPen(Qt::green));
        spectrumGraph->setName(QString("频谱"));
        if (plot->legend)
            spectrumGraph->addToLegend();

        connect(specRect->axis(QCPAxis::atBottom),
                static_cast<RangeChangedSig>(&QCPAxis::rangeChanged),
                m_owner, [this](const QCPRange &r) {
                    m_viewRange = QPair<double,double>(r.lower, r.upper);
                    m_autoFollow = false;
                    emit m_owner->rangeChanged(r.lower, r.upper);
                });
    } else if (type == View_Spectrum) {
        // 纯频谱：直接在默认轴矩形上画频谱曲线
        spectrumGraph = plot->addGraph();
        spectrumGraph->setAdaptiveSampling(true);    // 实时重绘按像素抽点，进一步降低绘制量
        spectrumGraph->setPen(QPen(Qt::green));
        spectrumGraph->setName(QString("频谱"));
        if (plot->legend)
            spectrumGraph->addToLegend();
    } else if (type == View_XY) {
        xyGraph = plot->addGraph();                  // 默认轴
        xyGraph->setAdaptiveSampling(true);          // 实时重绘按像素抽点，进一步降低绘制量
        xyGraph->setPen(QPen(Qt::blue));
    }
}

void ChartController::Private::clear()
{
    // 先移除频谱下半区（若存在），再清曲线
    if (specRect) {
        plot->plotLayout()->take(specRect);
        delete specRect;
        specRect = nullptr;
    }
    plot->clearGraphs();
    graphs.clear();
    m_channels.clear();
    spectrumGraph = nullptr;
    xyGraph = nullptr;
    xyChannels.clear();
    xyX.clear();
    xyY.clear();
    m_fullReplotPending = true;            // 结构变化：下一帧整图重绘
}

void ChartController::Private::clearData()
{
    // 只清数据不清图形：clearShow 后曲线在下轮刷新自动恢复
    for (QCPGraph *g : qAsConst(graphs))
        g->data()->clear();
    if (spectrumGraph)  spectrumGraph->data()->clear();
    if (xyGraph)        xyGraph->data()->clear();
}

void ChartController::Private::updateData(int channel, const QList<double>& x, const QList<double>& y)
{
    if (type == View_XY) {
        // A/B 由 setChannels 顺序决定：A 作 X，B 作 Y（按索引配对，要求采样对齐）
        const int idx = xyChannels.indexOf(channel);
        if (idx == 0)      xyX = y;
        else if (idx == 1) xyY = y;
        else               return;
        if (!xyGraph) return;
        const int n = qMin(xyX.size(), xyY.size());
        QVector<double> k(n), v(n);
        for (int i = 0; i < n; ++i) { k[i] = xyX[i]; v[i] = xyY[i]; }
        xyGraph->setData(k, v, true);
        m_fullReplotPending = true;             // XY 双轴自适应：整图重绘
        plot->xAxis->blockSignals(true);        // XY 双轴自适应（程序化，不触发 rangeChanged）
        plot->yAxis->blockSignals(true);
        plot->xAxis->rescale(true);
        plot->yAxis->rescale(true);
        plot->xAxis->blockSignals(false);
        plot->yAxis->blockSignals(false);
        return;
    }

    QCPGraph *graph = graphs.value(channel, nullptr);
    if (!graph) return;
    graph->setData(x, y, true);                 // Qt6 中 QList/QVector 同型，直接传入
    rescaleYToWindow();                         // Y 轴只对当前 X 窗口内的可见数据自适应
}

void ChartController::Private::updateSpectrum(const QList<double>& freq, const QList<double>& mag)
{
    if (!spectrumGraph) return;
    spectrumGraph->setData(freq, mag, true);
    // 频谱区双轴自适应（频率 + 幅值），程序化改范围不触发 rangeChanged
    m_fullReplotPending = true;            // 频谱轴自适应：整图重绘
    QCPAxis *kx = spectrumGraph->keyAxis();
    QCPAxis *vx = spectrumGraph->valueAxis();
    kx->blockSignals(true);
    vx->blockSignals(true);
    spectrumGraph->rescaleAxes(true);
    kx->blockSignals(false);
    vx->blockSignals(false);
}

void ChartController::Private::addChannel(int channel, const QString& name, const QColor& color)
{
    if (m_channels.contains(channel))    return;
    m_channels.append(channel);

    // 频谱/XY 视图不建波形曲线（频谱走 spectrumGraph，XY 走 xyGraph）
    if (type != View_Waveform && type != View_WaveformSpectrum) {
        if (type == View_XY) setXYChannels(m_channels);   // XY：A/B 映射跟随通道顺序
        m_fullReplotPending = true;        // 通道绑定变化：下一帧整图重绘
        return;
    }
    QCPGraph *graph = plot->addGraph();  // 默认轴（波形区）
    graph->setAdaptiveSampling(true);                     // 实时重绘按像素抽点，进一步降低绘制量
    graph->setName(name.isEmpty() ? QString("CH%1").arg(channel) : name);
    graph->setPen(QPen(color, 2));                        // 曲线加粗
    graph->setVisible(true);
    if (plot->legend) {
        graph->addToLegend();
        plot->legend->setVisible(true);
    }
    graphs.insert(channel, graph);
    m_fullReplotPending = true;            // 新增曲线：下一帧整图重绘
}

void ChartController::Private::removeChannel(int channel)
{
    m_channels.removeAll(channel);
    QCPGraph *graph = graphs.take(channel);
    if (graph) plot->removeGraph(graph);
    if (type == View_XY) setXYChannels(m_channels);
    m_fullReplotPending = true;            // 移除曲线：下一帧整图重绘
}

void ChartController::Private::setChannelVisible(int channel, bool visible)
{
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (!graph) return;
    graph->setVisible(visible);
    // 图例只列可见曲线
    if (visible) graph->addToLegend();
    else         graph->removeFromLegend();
    m_fullReplotPending = true;            // 图例/可见性变化：下一帧整图重绘
}

// Y 轴只对当前 X 窗口内的可见曲线取极值（避免历史极值把当前曲线压扁）
void ChartController::Private::rescaleYToWindow()
{
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    bool have = false;
    const double xLow  = plot->xAxis->range().lower;
    const double xHigh = plot->xAxis->range().upper;
    for (QCPGraph *g : qAsConst(graphs)) {
        if (!g->visible())   continue;
        const auto &d = g->data();
        for (auto it = d->findBegin(xLow); it != d->constEnd(); ++it) {
            if (it->key > xHigh)   break;
            yMin = qMin(yMin, it->value);
            yMax = qMax(yMax, it->value);
            have = true;
        }
    }
    if (!have)   return;
    if (yMin == yMax) { yMin -= 1.0; yMax += 1.0; }   // 平线也保留一点高度
    const double pad = (yMax - yMin) * 0.1;
    const double newMin = yMin - pad;
    const double newMax = yMax + pad;

    // 迟滞：新范围与当前范围差异小于阈值时不重设 Y 轴。
    const QCPRange cur     = plot->yAxis->range();
    const double spanRef   = qMax(qMax(cur.size(), newMax - newMin), 1e-9);
    const double diff      = qAbs(newMin - cur.lower) + qAbs(newMax - cur.upper);
    if (diff <= spanRef * 0.05)   return;             // 相对差异 ≤5%，沿用当前范围

    m_fullReplotPending = true;          // Y 轴范围变化：整图重绘（网格/刻度需重算）
    plot->yAxis->setRange(newMin, newMax);
}

void ChartController::Private::setViewVisible(bool visible)
{
    // 图例与可见性同步
    for (QCPGraph *graph : qAsConst(graphs)) {
        graph->setVisible(visible);
        if (visible) graph->addToLegend();
        else         graph->removeFromLegend();
    }
    if (spectrumGraph) {
        spectrumGraph->setVisible(visible);
        if (visible) spectrumGraph->addToLegend();
        else         spectrumGraph->removeFromLegend();
    }
    if (xyGraph) {
        xyGraph->setVisible(visible);
        if (visible) xyGraph->addToLegend();
        else         xyGraph->removeFromLegend();
    }
    m_fullReplotPending = true;            // 视图可见性变化：下一帧整图重绘
}

void ChartController::Private::setChannelName(int channel, const QString& name)
{
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (graph) graph->setName(name);
    m_fullReplotPending = true;            // 图例文字变化：下一帧整图重绘
}

void ChartController::Private::setChannelColor(int channel, const QColor& color)
{
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (graph) graph->setPen(QPen(color, 2));       // 曲线加粗
    m_fullReplotPending = true;            // 曲线颜色变化：下一帧整图重绘
}

void ChartController::Private::setChannels(const QList<int>& chs)
{
    m_channels.clear();
    // 只重建波形曲线；频谱/XY 特殊图形必须保留（clear 会误删）
    for (int ch : graphs.keys())
        plot->removeGraph(graphs.take(ch));
    for (int ch : chs)
        addChannel(ch);
    xyChannels = chs;                // XY：按顺序取前两个为 A/B
    m_fullReplotPending = true;      // 通道集合变化：下一帧整图重绘
}

void ChartController::Private::setXYChannels(const QList<int>& channels)
{
    xyChannels = channels;           // 仅更新 A/B 映射，不重建曲线
}

void ChartController::Private::setXRange(double min, double max)
{
    const QCPRange cur = plot->xAxis->range();
    if (qAbs(min - cur.lower) < 1e-9 && qAbs(max - cur.upper) < 1e-9)
        return;                          // 值未变（含 FP 微抖）：不触发重绘，网格/坐标轴缓冲可复用
    m_fullReplotPending = true;          // X 轴变化：整图重绘（网格/刻度需重算）
    // 程序化设范围不触发 rangeChanged（那是用户拖拽入口），否则会误关自动跟随
    // 只设置波形区（上）X 轴；频谱区（下）X 轴由频谱数据自行适配
    plot->xAxis->blockSignals(true);
    plot->xAxis->setRange(min, max);
    plot->xAxis->blockSignals(false);
}

void ChartController::Private::setYRange(double min, double max)
{
    plot->yAxis->setRange(min, max);
}

void ChartController::Private::setXAxisTitle(const QString& title)
{
    plot->xAxis->setLabel(title);
    if (specRect)
        specRect->axis(QCPAxis::atBottom)->setLabel(title);
}

void ChartController::Private::setYAxisTitle(const QString& title)
{
    plot->yAxis->setLabel(title);
    if (specRect)
        specRect->axis(QCPAxis::atLeft)->setLabel(title);
}

void ChartController::Private::setBackColor(const QColor& color)
{
    // Foreground follows background luminance: dark bg -> light ticks/labels; light bg -> dark.
    const bool   dark      = color.lightness() < 128;
    const QColor textColor = dark ? QColor(235, 235, 235) : QColor(40, 40, 40);
    const QColor axisColor = dark ? QColor(160, 160, 160) : QColor(40, 40, 40);

    const auto applyAxisStyle = [&](QCPAxis *axis) {
        if (!axis) return;
        axis->setBasePen(QPen(axisColor));
        axis->setTickPen(QPen(axisColor));
        axis->setSubTickPen(QPen(axisColor));
        axis->setTickLabelColor(textColor);
        axis->setLabelColor(textColor);
    };

    plot->setBackground(color);
    plot->axisRect()->setBackground(color);
    if (specRect)
        specRect->setBackground(color);

    applyAxisStyle(plot->xAxis);
    applyAxisStyle(plot->yAxis);
    if (specRect) {
        applyAxisStyle(specRect->axis(QCPAxis::atBottom));
        applyAxisStyle(specRect->axis(QCPAxis::atLeft));
    }

    m_fullReplotPending = true;          // 背景变化：下一帧整图重绘
}

void ChartController::Private::setViewRange(double min, double max)
{
    m_viewRange = QPair<double,double>(min, max);
    m_autoFollow = false;                    // 程序化设范围：切手动模式
    setXRange(min, max);                     // setXRange 内部 blockSignals，不触发 rangeChanged
}

void ChartController::Private::setAbsTime(bool enabled)
{
    m_absTime = enabled;
}

void ChartController::Private::setAutoFollow(bool enabled)
{
    if (!enabled) {
        const QCPRange r = plot->xAxis->range();
        m_viewRange = QPair<double,double>(r.lower, r.upper);   // 进手动前记住当前显示范围
    }
    m_autoFollow = enabled;
}

ViewType ChartController::Private::viewType() const                { return type; }
QList<int> ChartController::Private::channels() const              { return m_channels; }
bool ChartController::Private::absTime() const                     { return m_absTime; }
bool ChartController::Private::autoFollow() const                  { return m_autoFollow; }
QPair<double,double> ChartController::Private::viewRange() const   { return m_viewRange; }

void ChartController::Private::setInteraction(bool drag, bool zoom)
{
    QCP::Interactions it = QCP::Interaction(0);
    if (drag) it |= QCP::iRangeDrag;
    if (zoom) it |= QCP::iRangeZoom;
    plot->setInteractions(it);
}

void ChartController::Private::setRangeDragAxes(bool x, bool y)
{
    plot->axisRect()->setRangeDragAxes(
        x ? plot->xAxis : nullptr,
        y ? plot->yAxis : nullptr);
}

void ChartController::Private::setRangeZoomFactor(double xFactor, double yFactor)
{
    plot->axisRect()->setRangeZoomFactor(xFactor, yFactor);
}

void ChartController::Private::replot()
{
    plot->replot();
}

void ChartController::Private::replotQueued()
{
    QCPLayer *mainLayer = plot->layer("main");
    if (m_fullReplotPending || !mainLayer || mainLayer->mode() != QCPLayer::lmBuffered) {
        // 首帧 / 轴范围 / 外观 / 布局变化：整图排队重绘（所有缓冲层重画）
        m_fullReplotPending = false;
        plot->replot(QCustomPlot::rpQueuedReplot);
    } else {
        // 稳态：只重绘主层（曲线）缓冲，网格/坐标轴/背景缓冲复用。
        // 全量 replot 每帧重画所有层，本路径只画曲线层，可进一步降低开销
        mainLayer->replot();
    }
}

// ============================================================
// ChartController 公共接口：薄委托
// ============================================================
ChartController::ChartController(QWidget *parent)
    : QObject(parent)
    , pimpl(new Private(this, parent)) {}

ChartController::~ChartController()
{
    delete pimpl;
}

QWidget* ChartController::getWidget() const                          { return pimpl->getWidget(); }
void ChartController::setViewType(ViewType type)                     { pimpl->setViewType(type); }
void ChartController::clear()                                        { pimpl->clear(); }
void ChartController::clearData()                                    { pimpl->clearData(); }
void ChartController::updateData(int channel, const QList<double>& x, const QList<double>& y) { pimpl->updateData(channel, x, y); }
void ChartController::updateSpectrum(const QList<double>& freq, const QList<double>& mag)      { pimpl->updateSpectrum(freq, mag); }
void ChartController::addChannel(int channel, const QString& name, const QColor& color)        { pimpl->addChannel(channel, name, color); }
void ChartController::removeChannel(int channel)                     { pimpl->removeChannel(channel); }
void ChartController::setChannelVisible(int channel, bool visible)   { pimpl->setChannelVisible(channel, visible); }
void ChartController::setViewVisible(bool visible)                   { pimpl->setViewVisible(visible); }
void ChartController::setChannelName(int channel, const QString& name) { pimpl->setChannelName(channel, name); }
void ChartController::setChannelColor(int channel, const QColor& color) { pimpl->setChannelColor(channel, color); }
void ChartController::setChannels(const QList<int>& channels)        { pimpl->setChannels(channels); }
void ChartController::setXYChannels(const QList<int>& channels)      { pimpl->setXYChannels(channels); }
void ChartController::setXRange(double min, double max)              { pimpl->setXRange(min, max); }
void ChartController::setYRange(double min, double max)              { pimpl->setYRange(min, max); }
void ChartController::setXAxisTitle(const QString& title)            { pimpl->setXAxisTitle(title); }
void ChartController::setYAxisTitle(const QString& title)            { pimpl->setYAxisTitle(title); }
void ChartController::setBackColor(const QColor& color)              { pimpl->setBackColor(color); }
ViewType ChartController::viewType() const                           { return pimpl->viewType(); }
QList<int> ChartController::channels() const                         { return pimpl->channels(); }
bool ChartController::absTime() const                                { return pimpl->absTime(); }
bool ChartController::autoFollow() const                             { return pimpl->autoFollow(); }
QPair<double,double> ChartController::viewRange() const              { return pimpl->viewRange(); }
void ChartController::setViewRange(double min, double max)           { pimpl->setViewRange(min, max); }
void ChartController::setAbsTime(bool enabled)                       { pimpl->setAbsTime(enabled); }
void ChartController::setAutoFollow(bool enabled)                    { pimpl->setAutoFollow(enabled); }
void ChartController::setInteraction(bool drag, bool zoom)           { pimpl->setInteraction(drag, zoom); }
void ChartController::setRangeDragAxes(bool x, bool y)               { pimpl->setRangeDragAxes(x, y); }
void ChartController::setRangeZoomFactor(double xFactor, double yFactor) { pimpl->setRangeZoomFactor(xFactor, yFactor); }
void ChartController::replot()                                       { pimpl->replot(); }
void ChartController::replotQueued()                                 { pimpl->replotQueued(); }
