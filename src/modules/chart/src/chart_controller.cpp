#include "chart_controller.h"
#include "qcustomplot.h"
#include <QMouseEvent>

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
    void setChannelName(int channel, const QString& name);
    void setChannelColor(int channel, const QColor& color);
    void setChannels(const QList<int>& channels);
    void setXYChannels(const QList<int>& channels);
    void setXRange(double min, double max);
    void setYRange(double min, double max);
    void setXAxisTitle(const QString& title);
    void setYAxisTitle(const QString& title);
    void setBackColor(const QColor& color);
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
};

// ============================================================
// Private：真实逻辑
// ============================================================
ChartController::Private::Private(ChartController *owner, QWidget *parent)
    : m_owner(owner)
{
    plot = new QCustomPlot(parent);

    // 鼠标移动：上报坐标（高频，调用方自行节流）
    connect(plot, &QCustomPlot::mouseMove, owner, [this](QMouseEvent *e) {
        QCPAxis *x = plot->xAxis;
        QCPAxis *y = plot->yAxis;
        emit m_owner->mouseMove(x->pixelToCoord(e->pos().x()), y->pixelToCoord(e->pos().y()));
    });
    // X 轴范围变化：通知 ChartManager
    connect(plot->xAxis, static_cast<RangeChangedSig>(&QCPAxis::rangeChanged),
            owner, [this](const QCPRange &r) { emit m_owner->rangeChanged(r.lower, r.upper); });
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

    // 频谱下半区：需要时在默认轴矩形（波形区）下方插入一行
    if (type == View_WaveformSpectrum) {
        specRect = new QCPAxisRect(plot);
        plot->plotLayout()->insertRow(1);
        plot->plotLayout()->addElement(1, 0, specRect);

        spectrumGraph = plot->addGraph(
            specRect->axis(QCPAxis::atBottom),
            specRect->axis(QCPAxis::atLeft));
        spectrumGraph->setPen(QPen(Qt::green));
        spectrumGraph->setName(QString("频谱"));
        if (plot->legend)
            spectrumGraph->addToLegend();

        connect(specRect->axis(QCPAxis::atBottom),
                static_cast<RangeChangedSig>(&QCPAxis::rangeChanged),
                m_owner, [this](const QCPRange &r) { emit m_owner->rangeChanged(r.lower, r.upper); });
    } else if (type == View_Spectrum) {
        // 纯频谱：直接在默认轴矩形上画频谱曲线
        spectrumGraph = plot->addGraph();
        spectrumGraph->setPen(QPen(Qt::green));
        spectrumGraph->setName(QString("频谱"));
        if (plot->legend)
            spectrumGraph->addToLegend();
    } else if (type == View_XY) {
        xyGraph = plot->addGraph();        // 默认轴
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
    spectrumGraph = nullptr;
    xyGraph = nullptr;
    xyChannels.clear();
    xyX.clear();
    xyY.clear();
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
        return;
    }

    QCPGraph *graph = graphs.value(channel, nullptr);
    if (!graph) return;
    graph->setData(x, y, true);                 // Qt6 中 QList/QVector 同型，直接传入
}

void ChartController::Private::updateSpectrum(const QList<double>& freq, const QList<double>& mag)
{
    if (!spectrumGraph) return;
    spectrumGraph->setData(freq, mag, true);
}

void ChartController::Private::addChannel(int channel, const QString& name, const QColor& color)
{
    // 频谱/XY 视图不建波形曲线（频谱走 spectrumGraph，XY 走 xyGraph）
    if (type != View_Waveform && type != View_WaveformSpectrum) return;
    if (graphs.contains(channel))    return;
    QCPGraph *graph = plot->addGraph();  // 默认轴（波形区）
    graph->setName(name.isEmpty() ? QString("CH%1").arg(channel + 1) : name);
    graph->setPen(QPen(color));
    graph->setVisible(true);
    if (plot->legend) {
        graph->addToLegend();
        plot->legend->setVisible(true);
    }
    graphs.insert(channel, graph);
}

void ChartController::Private::removeChannel(int channel)
{
    QCPGraph *graph = graphs.take(channel);
    if (graph) plot->removeGraph(graph);
}

void ChartController::Private::setChannelVisible(int channel, bool visible)
{
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (graph) graph->setVisible(visible);
}

void ChartController::Private::setViewVisible(bool visible)
{
    for (QCPGraph *graph : qAsConst(graphs))
        graph->setVisible(visible);
    if (spectrumGraph)  spectrumGraph->setVisible(visible);
    if (xyGraph)        xyGraph->setVisible(visible);
}

void ChartController::Private::setChannelName(int channel, const QString& name)
{
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (graph) graph->setName(name);
}

void ChartController::Private::setChannelColor(int channel, const QColor& color)
{
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (graph) graph->setPen(QPen(color));
}

void ChartController::Private::setChannels(const QList<int>& channels)
{
    // 只重建波形曲线；频谱/XY 特殊图形必须保留（clear 会误删）
    for (int ch : graphs.keys())
        plot->removeGraph(graphs.take(ch));
    for (int ch : channels)
        addChannel(ch);
    xyChannels = channels;           // XY：按顺序取前两个为 A/B
}

void ChartController::Private::setXYChannels(const QList<int>& channels)
{
    xyChannels = channels;           // 仅更新 A/B 映射，不重建曲线
}

void ChartController::Private::setXRange(double min, double max)
{
    plot->xAxis->setRange(min, max);
    if (specRect)
        specRect->axis(QCPAxis::atBottom)->setRange(min, max);   // 上下同步
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
    plot->setBackground(color);
    plot->axisRect()->setBackground(color);
    if (specRect)
        specRect->setBackground(color);
}

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
    plot->replot(QCustomPlot::rpQueuedReplot);
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
void ChartController::setInteraction(bool drag, bool zoom)           { pimpl->setInteraction(drag, zoom); }
void ChartController::setRangeDragAxes(bool x, bool y)               { pimpl->setRangeDragAxes(x, y); }
void ChartController::setRangeZoomFactor(double xFactor, double yFactor) { pimpl->setRangeZoomFactor(xFactor, yFactor); }
void ChartController::replot()                                       { pimpl->replot(); }
void ChartController::replotQueued()                                 { pimpl->replotQueued(); }