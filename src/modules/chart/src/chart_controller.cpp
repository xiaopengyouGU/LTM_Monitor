#include "chart_controller.h"
#include "qcustomplot.h"
#include <QMouseEvent>
#include <limits>

// rangeChanged 有重载（单参/双参），统一强转单参版
using RangeChangedSig = void (QCPAxis::*)(const QCPRange&);
namespace
{
    // 主刻度保持 QCustomPlot 自动策略；只限制次刻度数量，避免小刻度过密。
    // 上限 1：两格次刻度在小屏上很难分辨，一格（主刻度中点）观感最好。
    class SparseSubTickTicker : public QCPAxisTicker
    {
    protected:
        int getSubTickCount(double tickStep) override
        {
            return qMin(1, QCPAxisTicker::getSubTickCount(tickStep));
        }
    };

    void applySparseSubTicks(QCPAxis *axis)
    {
        if (axis)
            axis->setTicker(QSharedPointer<QCPAxisTicker>(new SparseSubTickTicker));
    }
}

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
    void setXYData(const QList<double>& xValues, const QList<double>& yValues);
    void addChannel(int channel, const QString& name = QString(), const QColor& color = Qt::red);
    void removeChannel(int channel);
    void setChannelVisible(int channel, bool visible);
    void setViewVisible(bool visible);
    void rescaleYToWindow();
    void rescaleGraphY(QCPGraph *graph);
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
    // 内部工具：图例同步 / 窗口极值扫描 / 自适应范围（10% 余量 + 5% 迟滞）
    void applyVisible(QCPAbstractPlottable *item, bool visible, QCPLegend *legend = nullptr);
    bool scanWindow(const QCPGraph *graph, double xLow, double xHigh, double &yMin, double &yMax) const;
    bool rangeDiffers(const QCPAxis *axis, double newMin, double newMax) const;
    bool applyAutoRange(QCPAxis *axis, double vMin, double vMax);

    ChartController *m_owner = nullptr;
    QCustomPlot* plot = nullptr;
    ViewType     type = View_Waveform;
    QHash<int, QCPGraph*> graphs;          // 通道号 -> 波形曲线
    QCPGraph*    spectrumGraph = nullptr;  // 频谱曲线（WaveformSpectrum 下半区）
    QCPCurve*    xyCurve  = nullptr;       // XY 参数曲线（李萨如）
    QList<QCPAxisRect*> quadRects;         // 四宫格：2x2 轴矩形
    QList<QCPGraph*>    quadGraphs;        // 四宫格：每个面板一条曲线
    QList<QCPLegend*>   quadLegends;       // 四宫格：每个面板独立图例
    QCPAxisRect* specRect = nullptr;       // 频谱轴矩形
    QList<int>   xyChannels;               // XY：前两个 = A(X)、B(Y)
    QList<int>   m_channels;               // 视图绑定的通道列表（单一数据源，含 XY/频谱）
    QHash<int,bool> m_visible;             // 通道可见性缓存（波形/频谱/XY 共用）
    bool         m_absTime = false;        // true=绝对时间，false=相对时间
    bool         m_autoFollow = true;      // 自动跟随最新（用户拖拽/设范围后置 false）
    QPair<double,double> m_viewRange = {0.0, 0.0};   // 视图显示范围（手动模式时有效）
    bool         m_fullReplotPending = true;         // true=需要整图重绘（首帧/轴范围/外观/布局变化）
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
    // 图例一律显式挂载：关闭 QCustomPlot 默认的“新建 plottable 自动进图例”，
    // 否则四宫格每个面板的曲线都会被自动塞进默认图例（它挂在左上轴矩形上）。
    plot->setAutoAddPlottableToLegend(false);
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
    applySparseSubTicks(plot->xAxis);
    applySparseSubTicks(plot->yAxis);

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
    // 图例可见性：四宫格用每面板独立图例，XY 用轴标题标注，两者都不显示默认图例
    if (plot->legend)
        plot->legend->setVisible(type == View_Waveform || type == View_WaveformSpectrum || type == View_Spectrum);
    m_fullReplotPending = true;            // 结构重建：下一帧整图重绘

    // 频谱下半区：需要时在默认轴矩形（波形区）下方插入一行
    if (type == View_WaveformSpectrum) {
        specRect = new QCPAxisRect(plot);
        plot->plotLayout()->insertRow(1);
        plot->plotLayout()->addElement(1, 0, specRect);
        plot->xAxis->setLabel(QString("时间 [s]"));
        plot->yAxis->setLabel(QString("数值"));
        specRect->axis(QCPAxis::atBottom)->setLabel(QString("频率 [Hz]"));
        specRect->axis(QCPAxis::atLeft)->setLabel(QString("幅值"));
        applySparseSubTicks(specRect->axis(QCPAxis::atBottom));
        applySparseSubTicks(specRect->axis(QCPAxis::atLeft));

        spectrumGraph = plot->addGraph(
            specRect->axis(QCPAxis::atBottom),
            specRect->axis(QCPAxis::atLeft));
        spectrumGraph->setAdaptiveSampling(true);    // 实时重绘按像素抽点，进一步降低绘制量
        spectrumGraph->setPen(QPen(Qt::green, 2));
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
        spectrumGraph->setPen(QPen(Qt::green, 2));
        spectrumGraph->setName(QString("频谱"));
        plot->xAxis->setLabel(QString("频率 [Hz]"));
        plot->yAxis->setLabel(QString("幅值"));
        if (plot->legend)
            spectrumGraph->addToLegend();
    } else if (type == View_XY) {
        xyCurve = new QCPCurve(plot->xAxis, plot->yAxis); // 参数曲线：X/Y 原始数据
        xyCurve->setPen(QPen(Qt::blue, 2));
        plot->xAxis->setLabel(QString("X轴数值"));
        plot->yAxis->setLabel(QString("Y轴数值"));
    } else if (type == View_QuadGrid) {
        QCPLayoutGrid *layout = plot->plotLayout();
        layout->insertColumn(1);
        layout->insertRow(1);
        layout->setColumnStretchFactor(0, 1.0);
        layout->setColumnStretchFactor(1, 1.0);
        layout->setRowStretchFactor(0, 1.0);
        layout->setRowStretchFactor(1, 1.0);
        layout->setColumnSpacing(4);
        layout->setRowSpacing(4);

        quadRects.clear();
        quadGraphs.clear();
        if (plot->legend)
            plot->legend->setVisible(false);   // 四宫格改用每面板独立图例，默认图例不显示
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 2; ++col) {
                QCPAxisRect *rect = (row == 0 && col == 0) ? plot->axisRect()
                                                            : new QCPAxisRect(plot);
                if (row != 0 || col != 0)
                    layout->addElement(row, col, rect);

                QCPAxis *bx = rect->axis(QCPAxis::atBottom);
                QCPAxis *ly = rect->axis(QCPAxis::atLeft);
                QCPAxis *ty = rect->axis(QCPAxis::atTop);
                bx->setLabel(row == 1 ? QString("时间 [s]") : QString());
                ly->setLabel(col == 0 ? QString("数值") : QString());
                applySparseSubTicks(bx);
                applySparseSubTicks(ly);
                ty->setVisible(false);

                QCPLegend *legend = new QCPLegend;
                rect->insetLayout()->addElement(legend, Qt::AlignTop | Qt::AlignRight);
                legend->setLayer(QString("legend"));   // 图例图层：保证画在曲线/坐标轴之上
                legend->setVisible(true);

                QCPGraph *graph = plot->addGraph(bx, ly);
                graph->setAdaptiveSampling(true);
                graph->setPen(QPen(Qt::blue, 2));
                graph->setVisible(false);
                graph->addToLegend(legend);

                quadRects.append(rect);
                quadGraphs.append(graph);
                quadLegends.append(legend);
            }
        }
    } else {
        plot->xAxis->setLabel(QString("时间 [s]"));
        plot->yAxis->setLabel(QString("数值"));
    }
}

void ChartController::Private::clear()
{
    // 先删除所有 plottable，再删除 specRect，避免 graph 继续持有已释放的坐标轴。
    // 图例项持有 plottable 指针：先逐个摘掉四宫格图例项，再删曲线，避免悬挂指针
    for (int i = 0; i < quadGraphs.size() && i < quadLegends.size(); ++i)
        quadGraphs.at(i)->removeFromLegend(quadLegends.at(i));
    plot->clearGraphs();
    graphs.clear();
    spectrumGraph = nullptr;
    if (xyCurve) {
        plot->removePlottable(xyCurve);
        xyCurve = nullptr;
    }
    if (specRect) {
        plot->plotLayout()->take(specRect);
        delete specRect;
        specRect = nullptr;
    }
    for (QCPAxisRect *rect : quadRects) {
        if (rect && rect != plot->axisRect()) {
            plot->plotLayout()->take(rect);
            delete rect;
        }
    }
    quadRects.clear();
    quadGraphs.clear();
    plot->plotLayout()->simplify();         // 清掉动态插入后留下的空行/空列
    m_channels.clear();
    m_visible.clear();
    xyChannels.clear();
    m_fullReplotPending = true;            // 结构变化：下一帧整图重绘
}

void ChartController::Private::clearData()
{
    // 只清数据不清图形：clearShow 后曲线在下轮刷新自动恢复
    for (QCPGraph *g : qAsConst(graphs))
        g->data()->clear();
    if (spectrumGraph)  spectrumGraph->data()->clear();
    if (xyCurve)        xyCurve->data()->clear();
}

void ChartController::Private::updateData(int channel, const QList<double>& x, const QList<double>& y)
{
    if (type == View_XY) {
        // XY 由 ChartManager 在整批结果齐备后统一调用 setXYData()。
        return;
    }
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (!graph) return;
    graph->setData(x, y, true);                 // Qt6 中 QList/QVector 同型，直接传入
    if (type == View_QuadGrid)
        rescaleGraphY(graph);                   // 四宫格：每个面板独立自适应 Y 轴
    else
        rescaleYToWindow();                     // 单波形视图：共享 Y 轴自适应
}

void ChartController::Private::updateSpectrum(const QList<double>& freq, const QList<double>& mag)
{
    if (!spectrumGraph) return;
    spectrumGraph->setData(freq, mag, true);

    // 频率轴按当前数据自适应；Y 轴采用与波形视图相同的“余量 + 迟滞”策略。
    double maxMag = 0.0;
    for (double value : mag)
        maxMag = qMax(maxMag, value);
    const double yMin = 0.0;
    const double yMax = (maxMag > 0.0) ? (maxMag * 1.1) : 1.0;

    QCPAxis *kx = spectrumGraph->keyAxis();
    QCPAxis *vx = spectrumGraph->valueAxis();
    bool rangeChanged = false;

    kx->blockSignals(true);
    vx->blockSignals(true);

    if (m_autoFollow) {                      // 手动拖拽后不再改频率范围
        const QCPRange oldKey = kx->range();
        spectrumGraph->rescaleKeyAxis(true);
        const QCPRange newKey = kx->range();
        rangeChanged = (oldKey.lower != newKey.lower || oldKey.upper != newKey.upper);
    }

    // 迟滞：新的 Y 范围与当前范围差异小于 5% 时不重设，避免频谱上下抖动。
    if (rangeDiffers(vx, yMin, yMax)) {
        vx->setRange(yMin, yMax);
        rangeChanged = true;
    }

    kx->blockSignals(false);
    vx->blockSignals(false);

    if (rangeChanged)
        m_fullReplotPending = true;          // 轴范围变化：整图重绘
}

void ChartController::Private::setXYData(const QList<double>& xValues, const QList<double>& yValues)
{
    if (type != View_XY || !xyCurve) return;

    // X/Y 就是同一批结果里两个通道的原始数值，直接按下标配对写入。
    // 数据不足时保留上一帧，避免某一批不完整把李萨如曲线清空。
    if (xValues.size() < 2 || yValues.size() < 2) return;
    const int n = qMin(xValues.size(), yValues.size());
    QVector<double> k(n), v(n);
    for (int i = 0; i < n; ++i) {
        k[i] = xValues.at(i);
        v[i] = yValues.at(i);
    }
    QVector<double> t(n);
    for (int i = 0; i < n; ++i) t[i] = i;
    xyCurve->setData(t, k, v);

    double xMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    for (int i = 0; i < n; ++i) {
        xMin = qMin(xMin, k.at(i));
        xMax = qMax(xMax, k.at(i));
        yMin = qMin(yMin, v.at(i));
        yMax = qMax(yMax, v.at(i));
    }

    // X/Y 都按当前窗口数据自适应（与波形 Y 轴同一套余量 + 迟滞）；
    // 程序化改范围不触发 rangeChanged（那是用户拖拽入口）
    plot->xAxis->blockSignals(true);
    plot->yAxis->blockSignals(true);
    applyAutoRange(plot->xAxis, xMin, xMax);
    applyAutoRange(plot->yAxis, yMin, yMax);
    plot->xAxis->blockSignals(false);
    plot->yAxis->blockSignals(false);
}

// 曲线可见性与图例项同步：legend 为空表示挂到默认图例（四宫格用每面板独立图例）
void ChartController::Private::applyVisible(QCPAbstractPlottable *item, bool visible, QCPLegend *legend)
{
    if (!item) return;
    item->setVisible(visible);
    if (!legend) legend = plot->legend;
    if (!legend) return;
    if (visible) item->addToLegend(legend);
    else         item->removeFromLegend(legend);
}

void ChartController::Private::addChannel(int channel, const QString& name, const QColor& color)
{
    if (m_channels.contains(channel))    return;
    m_channels.append(channel);
    const bool visible = m_visible.value(channel, true);
    m_visible.insert(channel, visible);

    // 频谱/XY 视图不建波形曲线（频谱走 spectrumGraph，XY 走 xyCurve）
    if (type != View_Waveform && type != View_WaveformSpectrum) {
        if (type == View_XY) setXYChannels(m_channels);   // XY：A/B 映射跟随通道顺序
        m_fullReplotPending = true;        // 通道绑定变化：下一帧整图重绘
        return;
    }
    QCPGraph *graph = plot->addGraph();  // 默认轴（波形区）
    graph->setAdaptiveSampling(true);                     // 实时重绘按像素抽点，进一步降低绘制量
    graph->setName(name.isEmpty() ? QString("CH%1").arg(channel) : name);
    graph->setPen(QPen(color, 2));                        // 曲线加粗
    applyVisible(graph, visible);
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
    m_visible.insert(channel, visible);

    // 波形曲线：四宫格用本面板的独立图例，其余视图用默认图例
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (graph) {
        const int idx = (type == View_QuadGrid) ? m_channels.indexOf(channel) : -1;
        applyVisible(graph, visible, quadLegends.value(idx, nullptr));
    }

    // 频谱曲线只对应当前绑定的频谱通道
    if (spectrumGraph && m_channels.contains(channel))
        applyVisible(spectrumGraph, visible);

    // XY 曲线只有在 X/Y 两个通道都可见时才显示
    if (xyCurve && xyChannels.contains(channel)) {
        bool allVisible = true;
        for (int ch : xyChannels)
            allVisible = allVisible && m_visible.value(ch, true);
        applyVisible(xyCurve, allVisible);
    }
    m_fullReplotPending = true;            // 图例/可见性变化：下一帧整图重绘
}

// 扫描曲线在 [xLow, xHigh] 窗口内的 Y 极值；窗口内有点返回 true
bool ChartController::Private::scanWindow(const QCPGraph *graph, double xLow, double xHigh,
                                         double &yMin, double &yMax) const
{
    if (!graph) return false;
    bool have = false;
    const QSharedPointer<QCPGraphDataContainer> data = graph->data();
    for (auto it = data->findBegin(xLow); it != data->constEnd(); ++it) {
        if (it->key > xHigh) break;
        yMin = qMin(yMin, it->value);
        yMax = qMax(yMax, it->value);
        have = true;
    }
    return have;
}

// 迟滞判定：新范围与当前范围差异超过 5% 才认为“范围变了”
bool ChartController::Private::rangeDiffers(const QCPAxis *axis, double newMin, double newMax) const
{
    if (!axis) return false;
    const QCPRange cur = axis->range();
    const double spanRef = qMax(qMax(cur.size(), newMax - newMin), 1e-9);
    const double diff = qAbs(newMin - cur.lower) + qAbs(newMax - cur.upper);
    return diff > spanRef * 0.05;
}

// 自适应轴范围：10% 余量 + 5% 迟滞（差异不足阈值时沿用当前范围，避免抖动）
bool ChartController::Private::applyAutoRange(QCPAxis *axis, double vMin, double vMax)
{
    if (!axis) return false;
    if (vMin == vMax) { vMin -= 1.0; vMax += 1.0; }   // 平线也保留一点高度
    const double pad = (vMax - vMin) * 0.1;
    const double newMin = vMin - pad;
    const double newMax = vMax + pad;
    if (!rangeDiffers(axis, newMin, newMax)) return false;
    axis->setRange(newMin, newMax);
    m_fullReplotPending = true;          // 轴范围变化：整图重绘（网格/刻度需重算）
    return true;
}

// Y 轴只对当前 X 窗口内的可见曲线取极值（避免历史极值把当前曲线压扁）
void ChartController::Private::rescaleYToWindow()
{
    const QCPRange xr = plot->xAxis->range();
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    bool have = false;
    for (QCPGraph *g : qAsConst(graphs)) {
        if (!g->visible())   continue;
        if (scanWindow(g, xr.lower, xr.upper, yMin, yMax))
            have = true;
    }
    if (!have)   return;
    applyAutoRange(plot->yAxis, yMin, yMax);
}

void ChartController::Private::rescaleGraphY(QCPGraph *graph)
{
    if (!graph || !graph->visible()) return;
    const QCPRange xr = graph->keyAxis()->range();
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    if (!scanWindow(graph, xr.lower, xr.upper, yMin, yMax))
        return;
    applyAutoRange(graph->valueAxis(), yMin, yMax);   // 四宫格：每个面板独立自适应
}

void ChartController::Private::setViewVisible(bool visible)
{
    if (type == View_QuadGrid) {
        const int n = qMin(quadGraphs.size(), m_channels.size());
        for (int i = 0; i < quadGraphs.size(); ++i) {
            const bool on = visible && i < n && m_visible.value(m_channels.at(i), true);
            applyVisible(quadGraphs.at(i), on, quadLegends.value(i, nullptr));
        }
        m_fullReplotPending = true;
        return;
    }

    for (QCPGraph *graph : qAsConst(graphs))
        applyVisible(graph, visible);
    if (spectrumGraph) applyVisible(spectrumGraph, visible);
    if (xyCurve)       applyVisible(xyCurve, visible);
    m_fullReplotPending = true;            // 视图可见性变化：下一帧整图重绘
}

void ChartController::Private::setChannelName(int channel, const QString& name)
{
    const QString label = name.isEmpty() ? QString("CH%1").arg(channel) : name;   // 空名回退 CHn：图例始终有可读文字
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (graph)
        graph->setName(label);
    if (spectrumGraph && m_channels.contains(channel))
        spectrumGraph->setName(label);       // 频谱图例跟随通道名
    m_fullReplotPending = true;            // 图例文字变化：下一帧整图重绘
}

void ChartController::Private::setChannelColor(int channel, const QColor& color)
{
    // 频谱固定绿色、XY 固定蓝色；通道颜色只作用于波形曲线。
    QCPGraph *graph = graphs.value(channel, nullptr);
    if (graph) graph->setPen(QPen(color, 2));
    m_fullReplotPending = true;            // 曲线颜色变化：下一帧整图重绘
}

void ChartController::Private::setChannels(const QList<int>& chs)
{
    if (type == View_QuadGrid) {
        m_channels.clear();
        graphs.clear();
        for (int i = 0; i < quadGraphs.size(); ++i) {
            QCPGraph *graph = quadGraphs.at(i);
            QCPLegend *legend = quadLegends.value(i, nullptr);
            graph->data()->clear();
            if (i < chs.size()) {
                const int ch = chs.at(i);
                m_channels.append(ch);
                graphs.insert(ch, graph);
                graph->setName(QString("CH%1").arg(ch));
                graph->setPen(QPen(Qt::blue, 2));
                applyVisible(graph, m_visible.value(ch, true), legend);
            } else {
                applyVisible(graph, false, legend);        // 没有通道的面板不画
            }
        }
        m_fullReplotPending = true;
        return;
    }

    m_channels.clear();

    // 通道映射变化：先清掉旧波形、频谱、XY 曲线数据，避免残留旧曲线。
    for (int ch : graphs.keys())
        plot->removeGraph(graphs.take(ch));
    if (spectrumGraph) spectrumGraph->data()->clear();
    if (xyCurve)       xyCurve->data()->clear();

    for (int ch : chs)
        addChannel(ch);
    setXYChannels(chs);              // XY：按顺序取前两个为 A/B（唯一赋值点）
    if ((type == View_Spectrum || type == View_WaveformSpectrum) && spectrumGraph && !chs.isEmpty())
        spectrumGraph->setName(QString("CH%1").arg(chs.first()));
    m_fullReplotPending = true;      // 通道集合变化：下一帧整图重绘
}

void ChartController::Private::setXYChannels(const QList<int>& channels)
{
    xyChannels = channels;           // 仅更新 A/B 映射，不重建曲线
}

void ChartController::Private::setXRange(double min, double max)
{
    if (type == View_QuadGrid) {
        for (QCPAxisRect *rect : qAsConst(quadRects)) {
            QCPAxis *axis = rect->axis(QCPAxis::atBottom);
            axis->blockSignals(true);
            axis->setRange(min, max);
            axis->blockSignals(false);
        }
        m_fullReplotPending = true;
        return;
    }

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
    if (type == View_QuadGrid) {
        for (QCPAxisRect *rect : qAsConst(quadRects))
            rect->axis(QCPAxis::atBottom)->setLabel(title);
    } else {
        plot->xAxis->setLabel(title);
    }
}

void ChartController::Private::setYAxisTitle(const QString& title)
{
    if (type == View_QuadGrid) {
        for (QCPAxisRect *rect : qAsConst(quadRects))
            rect->axis(QCPAxis::atLeft)->setLabel(title);
    } else {
        plot->yAxis->setLabel(title);
    }
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
    // 图例底板/文字随背景明暗：深色背景用浅色文字，避免图例文字看不见
    const auto applyLegendStyle = [&](QCPLegend *lg) {
        if (!lg) return;
        lg->setTextColor(textColor);
        lg->setBrush(QBrush(dark ? QColor(45, 45, 45, 210) : QColor(255, 255, 255, 210)));
        lg->setBorderPen(QPen(axisColor));
    };
    applyLegendStyle(plot->legend);
    for (QCPLegend *lg : qAsConst(quadLegends))
        applyLegendStyle(lg);
    if (type == View_QuadGrid) {
        for (QCPAxisRect *rect : qAsConst(quadRects)) {
            rect->setBackground(color);
            applyAxisStyle(rect->axis(QCPAxis::atBottom));
            applyAxisStyle(rect->axis(QCPAxis::atLeft));
            applyAxisStyle(rect->axis(QCPAxis::atTop));
        }
    } else {
        plot->axisRect()->setBackground(color);
        if (specRect)
            specRect->setBackground(color);
        applyAxisStyle(plot->xAxis);
        applyAxisStyle(plot->yAxis);
        if (specRect) {
            applyAxisStyle(specRect->axis(QCPAxis::atBottom));
            applyAxisStyle(specRect->axis(QCPAxis::atLeft));
        }
    }

    m_fullReplotPending = true;              // 背景变化：下一帧整图重绘
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
    if (type == View_QuadGrid) {
        for (QCPAxisRect *rect : qAsConst(quadRects))
            rect->setRangeDragAxes(
                x ? rect->axis(QCPAxis::atBottom) : nullptr,
                y ? rect->axis(QCPAxis::atLeft) : nullptr);
        return;
    }
    plot->axisRect()->setRangeDragAxes(
        x ? plot->xAxis : nullptr,
        y ? plot->yAxis : nullptr);
}

void ChartController::Private::setRangeZoomFactor(double xFactor, double yFactor)
{
    if (type == View_QuadGrid) {
        for (QCPAxisRect *rect : qAsConst(quadRects))
            rect->setRangeZoomFactor(xFactor, yFactor);
        return;
    }
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
ChartController::ChartController(QWidget *parent) : QObject(parent), pimpl(new Private(this, parent)) {}
ChartController::~ChartController()                                  { delete pimpl; }
QWidget* ChartController::getWidget() const                          { return pimpl->getWidget(); }
void ChartController::setViewType(ViewType type)                     { pimpl->setViewType(type); }
void ChartController::clear()                                        { pimpl->clear(); }
void ChartController::clearData()                                    { pimpl->clearData(); }
void ChartController::updateData(int channel, const QList<double>& x, const QList<double>& y) { pimpl->updateData(channel, x, y); }
void ChartController::updateSpectrum(const QList<double>& freq, const QList<double>& mag)      { pimpl->updateSpectrum(freq, mag); }
void ChartController::setXYData(const QList<double>& xValues, const QList<double>& yValues) { pimpl->setXYData(xValues, yValues); }
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
