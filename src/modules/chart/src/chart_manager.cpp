#include "chart_manager_private.h"
#include "chart_controller.h"
#include "data_storage.h"
#include "data_importer.h"
#include "data_exporter.h"
#include "chart_def.h"

#include <QtConcurrent>
#include <QThread>
#include <QDateTime>
#include <QDebug>
#include <cmath>

// 简单原地 FFT：实数输入 -> 幅度谱（定义见文件末尾，公共接口之前）
static void fft_real(QList<double>& re, QList<double>& im, int n);   // im 为调用方提供的虚部工作区（如 raw.times）

// 默认曲线颜色：20 色表按通道号取模循环，任意通道都有稳定默认色
QColor getChannelColor(int channel)
{
    if (channel < 0)   return Qt::black;
    static const QColor colors[] = {
        Qt::red,                     // 红
        QColor(255, 128, 0),         // 橙
        Qt::magenta,                 // 品红
        Qt::blue,                    // 蓝
        Qt::darkBlue,                // 深蓝
        Qt::green,                   // 绿
        Qt::yellow,                  // 黄
        Qt::darkGreen,               // 深绿
        Qt::cyan,                    // 青
        Qt::darkYellow,              // 深黄
        QColor(128, 0, 128),         // 紫
        QColor(255, 192, 203),       // 粉
        QColor(139, 69, 19),         // 棕
        QColor(85, 107, 47),         // 橄榄绿
        QColor(75, 0, 130),          // 靛蓝
        QColor(144, 238, 144),       // 浅绿
        QColor(255, 165, 0),         // 橙（与目标值橙色不同）
        QColor(138, 43, 226),        // 紫罗兰
        QColor(135, 206, 235),       // 天蓝
        QColor(255, 127, 80)         // 珊瑚
    };
    const int n = int(sizeof(colors) / sizeof(colors[0]));
    return colors[channel % n];
}

// ============================================================
// ChartManager::Private：模型(DataStorage) + 视图集合(ChartController) 的调度层
// 数据写入委托给 DataStorage；显示由各 ChartController 负责。
// ============================================================

ChartManager::Private::Private(int channelCount, ChartManager *parent)
    : QObject(parent), m_manager(parent), m_channelCount(channelCount)
{
    m_channelNames.fill(QString(), m_channelCount);        // 通道名初始为空，导出时回退 ChN
    m_channelVisible.fill(true, m_channelCount);           // 通道默认可见
    m_fftBufs.resize(m_channelCount);                      // 每通道一块频谱缓冲，并行任务零锁写入
    m_storage = new DataStorage(MAX_CHANNEL_POINTS, m_channelCount, this);

    // 导入导出工作在线程中：DataImporter / DataExporter 移入 data_thread（无 parent）
    m_dataThread = new QThread(this);
    m_importer   = new DataImporter(m_storage);
    m_exporter   = new DataExporter(m_storage);
    m_importer->moveToThread(m_dataThread);
    m_exporter->moveToThread(m_dataThread);
    // 绑定导入导出相关信号
    connect(this, &Private::dataImport, m_importer, &DataImporter::do_dataImport);
    connect(this, &Private::dataExport, m_exporter, &DataExporter::do_dataExport);
    connect(m_importer, &DataImporter::importFinished, this, &Private::do_importFinished);
    connect(m_importer, &DataImporter::importNames, this, &Private::do_importNames);
    connect(m_exporter, &DataExporter::exportFinished, this, &Private::do_exportFinished);
    m_dataThread->start();

    m_timer = new QTimer(this);
    m_timer->setInterval(m_period);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &Private::updateData);
}

ChartManager::Private::~Private()
{
    if (m_activeWatcher)
        m_activeWatcher->waitForFinished();      // 避免析构时任务还在写缓冲
    for (QFutureWatcher<ParallelResult> *w : qAsConst(m_retiredWatchers))
        w->waitForFinished();                    // 被丢弃的批次也要等任务结束，避免写已析构缓冲
    if (m_dataThread && m_dataThread->isRunning()) {
        m_dataThread->quit();
        m_dataThread->wait();
    }
    delete m_importer;        // 线程已停，手动析构移入线程的对象
    delete m_exporter;
}

// ====== 生命周期 ======
void ChartManager::Private::start()   { m_timer->start(); }
void ChartManager::Private::stop()    { m_timer->stop(); }

void ChartManager::Private::setPeriod(int ms)
{
    if (ms < 20) ms = 20;
    m_period = ms;
    m_timer->setInterval(ms);
}

void ChartManager::Private::setMode(ShowMode mode)
{
    if (mode == m_mode)   return;
    m_mode = mode;
    for (ChartController *ctrl : qAsConst(controllers))
        ctrl->setAutoFollow(mode == Mode_Auto);
    m_forceRefresh = true;
    updateData();                        // 立即按新模式刷新
}

ShowMode ChartManager::Private::getMode() const
{
    return m_mode;
}

// ====== 数据写入（全部委托给模型） ======
void ChartManager::Private::addData(int channel, double value)
{
    m_storage->addData(channel, -1.0, value);       // -1 表示自动打时间戳
}

void ChartManager::Private::addData(int channel, double time, double value)
{
    m_storage->addData(channel, time, value);
}

void ChartManager::Private::addData(const QList<ChannelData>& dataList)
{
    m_storage->addData(dataList);
}

// ====== 视图管理 ======
int ChartManager::Private::createView(ViewType type)
{
    ChartController *ctrl = new ChartController();   // 控件由本对象持有，无父对象
    ctrl->setViewType(type);
    ctrl->setInteraction(true, true);
    ctrl->setRangeDragAxes(true, true);
    ctrl->setRangeZoomFactor(1.2, 1.2);
    ctrl->setBackColor(Qt::white);

    const int idx = controllers.size();
    // 用户拖拽/缩放：controller 内部已维护范围并切手动模式，这里只触发强制重绘
    connect(ctrl, &ChartController::rangeChanged, this, [this](double, double) {
        m_forceRefresh = true;   // 用户拖拽/缩放：强制按新范围重绘
        updateData();
    });

    controllers.append(ctrl);
    return idx;
}

void ChartManager::Private::removeView(int viewIndex)
{
    if (viewIndex == -1) {
        // 移除所有视图（从后往前删，下标不失效）
        for (int i = controllers.size() - 1; i >= 0; --i)
            removeView(i);
        return;
    }
    if (viewIndex < 0 || viewIndex >= controllers.size()) return;
    delete controllers[viewIndex];
    controllers.removeAt(viewIndex);
}

QWidget* ChartManager::Private::getViewWidget(int viewIndex) const
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return nullptr;
    return controllers[viewIndex]->getWidget();
}

// ====== 通道绑定（每个视图一份订阅列表，通道可跨视图共享） ======
void ChartManager::Private::attachChannel(int viewIndex, int channel)
{
    if (viewIndex < 0 || viewIndex >= controllers.size())   return;
    if (channel < 0 || channel >= m_channelCount)           return;
    ChartController *ctrl = controllers[viewIndex];
    if (ctrl->channels().contains(channel))                 return;
    ctrl->addChannel(channel, m_channelNames[channel], getChannelColor(channel));   // 默认颜色，可再 setChannelColor 覆盖
    m_forceRefresh = true;
    updateData();
}

void ChartManager::Private::detachChannel(int viewIndex, int channel)
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return;
    controllers[viewIndex]->removeChannel(channel);
    m_forceRefresh = true;
    updateData();
}

void ChartManager::Private::setViewChannels(int viewIndex, const QList<int>& channels)
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return;
    QList<int> valid;
    for (int ch : channels) {
        if (ch >= 0 && ch < m_channelCount)
            valid.append(ch);
    }
    ChartController *ctrl = controllers[viewIndex];
    ctrl->setChannels(valid);
    for (int ch : valid) {
        ctrl->setChannelName(ch, m_channelNames[ch]);      // 曲线带当前通道名
        ctrl->setChannelColor(ch, getChannelColor(ch));    // 默认颜色
        ctrl->setChannelVisible(ch, m_channelVisible.value(ch, true));   // 新映射继承全局可见性
    }
    m_forceRefresh = true;
    updateData();
}

QList<int> ChartManager::Private::getViewChannels(int viewIndex) const
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return QList<int>();
    return controllers[viewIndex]->channels();
}

// ====== 通道控制（全局生效，应用到所有视图） ======
void ChartManager::Private::setChannelName(int channel, const QString& name)
{
    if (channel >= 0 && channel < m_channelNames.size())
        m_channelNames[channel] = name;                  // 记录通道名，导出列名用
    for (ChartController *ctrl : controllers)
        ctrl->setChannelName(channel, name);
    m_forceRefresh = true;   // 外观变化：下个刷新周期重绘
}

void ChartManager::Private::setChannelColor(int channel, const QColor& color)
{
    for (ChartController *ctrl : controllers)
        ctrl->setChannelColor(channel, color);
    m_forceRefresh = true;   // 外观变化：下个刷新周期重绘
}

void ChartManager::Private::setChannelVisible(int channel, bool visible)
{
    if (channel >= 0 && channel < m_channelVisible.size())
        m_channelVisible[channel] = visible;             // 刷新只取可见通道
    for (ChartController *ctrl : controllers)
        ctrl->setChannelVisible(channel, visible);
    m_forceRefresh = true;   // 可见性变化：下个刷新周期重绘
}

// ====== 坐标轴 ======
void ChartManager::Private::setViewRange(int viewIndex, double startTime, double endTime)
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return;
    controllers[viewIndex]->setViewRange(startTime, endTime);   // controller 内部切手动模式
    m_forceRefresh = true;
    updateData();
}

void ChartManager::Private::setWindowLen(double seconds)
{
    if (seconds <= 0.0) return;
    m_windowLen = seconds;
    m_forceRefresh = true;   // 仅标记，下一轮定时刷新按新窗口取数
}

void ChartManager::Private::setAbsTime(int viewIndex, bool enabled)
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return;
    controllers[viewIndex]->setAbsTime(enabled);
    m_forceRefresh = true;
    updateData();
}

void ChartManager::Private::setBackColor(int viewIndex, int color)
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return;
    controllers[viewIndex]->setBackColor(color == 0 ? Qt::white : Qt::black);
    m_forceRefresh = true;   // 外观变化：下个刷新周期重绘
}

// ====== 每轮更新：窗口 -> 去重取数（异步） -> 广播 ======
void ChartManager::Private::updateData()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (isUpdating) {
        // 看门狗：上一批超过 1s 未完成（任务异常/信号丢失），丢弃并复位，避免刷新永久停摆
        if (nowMs - m_lastUpdateMs > 1000) {
            if (m_activeWatcher) {
                disconnect(m_activeWatcher, nullptr, this, nullptr);   // 旧任务跑完也不广播
                m_activeWatcher->future().cancel();                    // 取消：清掉线程池排队的任务，避免积压
                m_retiredWatchers.append(m_activeWatcher);
                m_activeWatcher = nullptr;
            }
            isUpdating = false;
            m_forceRefresh = true;   // 下轮强制重取
            return;
        } else {
            return;                  // 上一批未完成或导入中，跳过（并发保护）
        }
    }
    isUpdating = true;
    m_lastUpdateMs = nowMs;

    // 0. 批次快照：视图配置统一从 controller 拷贝。并行取数与广播都用同一份快照，
    //    任务运行期间改视图配置只影响下一批（与取数窗口的批次语义一致）
    snaps.clear();
    for (ChartController *ctrl : qAsConst(controllers))
        snaps.append(ViewSnapshot{ reinterpret_cast<quintptr>(ctrl), ctrl->viewType(),
                                   ctrl->channels(), ctrl->absTime(), ctrl->autoFollow(),
                                   ctrl->viewRange() });

    // 1. 可见通道去重 + 频谱通道集合 + 需要平移时间轴的通道集合
    QList<int> channels;
    QList<int> fftChannels;
    QSet<int> needShiftChannels;
    for (const ViewSnapshot &s : qAsConst(snaps)) {
        const bool isSpec = (s.type == View_Spectrum || s.type == View_WaveformSpectrum);
        const bool isTime = (s.type == View_Waveform || s.type == View_WaveformSpectrum || s.type == View_QuadGrid);
        for (int ch : s.channels) {
            if (!m_channelVisible.value(ch, true))   continue;   // 只处理可见通道（V0.2 同款，快速数据不卡）
            if (!channels.contains(ch))
                channels.append(ch);
            if (isSpec && !fftChannels.contains(ch))
                fftChannels.append(ch);
            // 只有绑定到"相对时间波形视图"的通道才需要平移轴；
            // XY 视图不读时间轴（updateData 只用 y 值），纯频谱视图不收波形
            if (isTime && !s.absTime)
                needShiftChannels.insert(ch);
        }
    }
    if (channels.isEmpty()) { isUpdating = false; return; }

    // 无新数据且非强制刷新（拖拽/换通道/停显等）则跳过，省一次并行取数与重绘
    if (!m_forceRefresh && m_storage->dataVersion() == m_lastVersion) {
        isUpdating = false;
        return;
    }
    m_forceRefresh = false;
    m_lastVersion  = m_storage->dataVersion();

    // 2. 窗口终点必须追到存储当前最新时间，否则永远只取到上次窗口，曲线不会滚动
    for (int ch : channels) {
        ChannelData data = m_storage->getData(ch, 1);   // 取最新的一个点
        const double t = (data.times.isEmpty()) ? 0 : data.times[0];
        if (t > m_latest)   m_latest = t;
    }
    // 2. 全局窗口：优先手动视图范围，否则自动跟随最新
    double start = -1.0, end = -1.0;            // <0 表示取全部
    for (const ViewSnapshot &s : qAsConst(snaps)) {
        // 只有时间型视图（波形/波形+频谱）的手动范围才是时间坐标；XY/频谱的显示坐标不能当全局窗口
        if (!s.autoFollow && (s.type == View_Waveform || s.type == View_WaveformSpectrum)) {
            start = s.range.first;
            end   = s.range.second;
            break;
        }
    }
    if (start < 0.0 || end < 0.0) {
        if (m_latest > 0.0) {
            start = m_latest - m_windowLen;
            end   = m_latest;
        }
        // m_latest <= 0：首轮取全部数据引导，结果回填 m_latest
    }

    // 2.5 自动跟随视图：把显示窗口应用到 X 轴（相对视图显示坐标以最新为 0）
    // 运行前 30s 窗口下限 start<0，内部时间戳从 0 起，下限钳到 0 即可
    if (end > start && end > 0.0) {
        const double x0 = qMax(start, 0.0);
        for (int i = 0; i < snaps.size(); ++i) {
            const ViewSnapshot &s = snaps[i];
            if (!s.autoFollow)   continue;
            // 频谱/XY 的 X 轴自行适配（频率/通道值），不套时间窗口
            if (s.type == View_Spectrum || s.type == View_XY)   continue;
            if (s.absTime)            controllers[i]->setXRange(x0, end);
            else                      controllers[i]->setXRange(-(end - x0), 0.0);
        }
    }

    // 3. 启动异步并行取数（线程池执行，主线程不被阻塞）
    const double winStart = start, winEnd = end;
    QFuture<ParallelResult> future = QtConcurrent::mapped(
        channels, [this, winStart, winEnd, fftChannels, needShiftChannels](int ch) -> ParallelResult {
            ParallelResult res;
            try {
                res.data     = m_storage->getData(ch, winStart, winEnd, m_threshold, /*isLTTB*/ false);
                // 相对时间轴平移也在并行任务中完成，且只复制需要的通道；
                // 主线程广播时零拷贝复用，避免 COW detach
                if (needShiftChannels.contains(ch) && !res.data.times.isEmpty()) {
                    res.shiftedTimes = res.data.times;
                    const double maxX = res.shiftedTimes.last();
                    for (double &t : res.shiftedTimes) t -= maxX;
                }
                res.spectrum = nullptr;
                if (fftChannels.contains(ch)) {
                    computeFFTInto(m_fftBufs[ch], ch, m_nfft);   // 频谱计算也在并行任务中
                    res.spectrum = &m_fftBufs[ch];
                }
            } catch (...) {
                res.data = ChannelData{};   // 单通道失败返回空数据，不影响整批
            }
            return res;
        });

    // 4. QFutureWatcher 异步收尾：完成后再广播，主线程零阻塞
    QFutureWatcher<ParallelResult> *watcher = new QFutureWatcher<ParallelResult>(this);
    m_activeWatcher = watcher;
    connect(watcher, &QFutureWatcher<ParallelResult>::finished, this, [this, watcher]() {
        QList<ParallelResult> results;
        try { results = watcher->future().results(); }   // finished 后 results() 不阻塞
        catch (...) { results.clear(); }
        try { broadcastResults(results); }
        catch (...) { isUpdating = false; }              // 广播异常也不能卡死刷新
        watcher->deleteLater();
        if (m_activeWatcher == watcher) m_activeWatcher = nullptr;
    });
    watcher->setFuture(future);
}

// 批量更新期间临时禁用控件重绘，作用域结束恢复（异常安全：中途抛异常也不会残留禁用态）
struct WidgetUpdatesGuard
{
    explicit WidgetUpdatesGuard(const QList<ChartController*>& cs)
        : controllers(cs)
    {
        for (ChartController *c : controllers)
            c->getWidget()->setUpdatesEnabled(false);
    }
    ~WidgetUpdatesGuard()
    {
        for (ChartController *c : controllers)
            c->getWidget()->setUpdatesEnabled(true);
    }
    QList<ChartController*> controllers;
};

// ====== 结果就绪（主线程）：隐式共享广播到所有订阅视图 ======
void ChartManager::Private::broadcastResults(const QList<ParallelResult>& results)
{
    // 批次快照与当前视图对齐：任务运行期间视图被增删时，下标错开的视图本帧跳过
    const int n = qMin(snaps.size(), controllers.size());

    // 批量更新期间禁用控件重绘，结束后统一启用并排队刷新一次（与 V0.2 同款，避免中间状态多次重绘）
    WidgetUpdatesGuard updatesGuard(controllers);

    for (const ParallelResult &r : results) {
        const ChannelData &data = r.data;
        if (data.times.isEmpty())   continue;

        // 时间戳单调递增：last 即该通道最新时间，下标访问即可得范围
        m_latest = qMax(m_latest, data.times.last());

        // ChannelData 自带 channel，直接按快照广播，无需哈希；
        // 相对时间轴已在并行任务中平移好（shiftedTimes），多视图共享同一份，主线程零拷贝
        for (int vi = 0; vi < n; ++vi) {
            const ViewSnapshot &s = snaps[vi];
            if (s.ctrl != reinterpret_cast<quintptr>(controllers[vi]))   continue;
            if (s.type == View_Spectrum || s.type == View_XY)   continue;   // 频谱/XY 单独批量投递
            if (!s.channels.contains(data.channel))   continue;
            ChartController *ctrl = controllers[vi];
            if (!s.absTime)
                ctrl->updateData(data.channel, r.shiftedTimes, data.values);   // 相对时间
            else
                ctrl->updateData(data.channel, data.times, data.values);       // 绝对时间
        }
    }


    // XY 视图：整批结果齐备后只提交一次，避免同一批内出现“新 X + 旧 Y”的半更新画面。
    for (int vi = 0; vi < n; ++vi) {
        const ViewSnapshot &s = snaps[vi];
        if (s.type != View_XY || s.channels.size() < 2)   continue;

        const ChannelData *xData = nullptr;
        const ChannelData *yData = nullptr;
        for (const ParallelResult &r : results) {
            if (r.data.channel == s.channels.at(0))   xData = &r.data;
            if (r.data.channel == s.channels.at(1))   yData = &r.data;
        }
        if (xData && yData)
            controllers[vi]->setXYData(xData->values, yData->values);
    }

    // 频谱视图：取绑定通道的频谱缓冲，零拷贝广播（多视图共享同一份）
    for (const ParallelResult &r : results) {
        if (!r.spectrum || r.spectrum->times.isEmpty())   continue;
        const int ch = r.spectrum->channel;
        for (int vi = 0; vi < n; ++vi) {
            const ViewSnapshot &s = snaps[vi];
            if (s.ctrl != reinterpret_cast<quintptr>(controllers[vi]))       continue;
            if (s.type != View_Spectrum && s.type != View_WaveformSpectrum)  continue;
            if (!s.channels.contains(ch))   continue;
            controllers[vi]->updateSpectrum(r.spectrum->times, r.spectrum->values);
        }
    }

    for (ChartController *ctrl : controllers)
        ctrl->replotQueued();

    isUpdating = false;                       // 本批完成：复位并发标志（关键），下一轮定时刷新才能启动
}

// ====== FFT：数据取自模型最近窗口，采样率由时间轴估算 ======
// 同步接口（一次性/分析用）；频谱显示走 updateData 并行管线，不占 UI 线程
ChannelData ChartManager::Private::computeFFT(int channel, int nfft)
{
    ChannelData out;
    computeFFTInto(out, channel, nfft);
    return out;
}

// 取最近 nfft 个点 -> 平均采样率 -> 汉宁窗 -> 原地 FFT -> 线性幅值谱，结果写 out
void ChartManager::Private::computeFFTInto(ChannelData &out, int channel, int nfft)
{
    if (channel < 0 || channel >= m_channelCount || nfft <= 0) return;

    // FFT 点数向下取整到 2 的幂（radix-2），同时保证不截断最近数据
    int N = 1;
    while ((N << 1) <= nfft)  N <<= 1;
    if (N < 4) return;

    ChannelData raw = m_storage->getData(channel, N);   // 实时窗口：最近 N 个点
    if (raw.times.size() < 4) return;
    const int n = raw.times.size();

    // 平均采样频率：由相邻时间戳间隔的平均值估算（fs = (n-1) / ΣΔt）
    double rate_N = 1.0;            // rate/N, 预计算常量，减少后续运算开销
    double dtSum = 0.0;
    for (int i = 1; i < n; ++i)
        dtSum += raw.times[i] - raw.times[i - 1];
    if (dtSum > 0.0)
        rate_N = (n - 1) / dtSum / N;

    // 汉宁窗 + 标准单边幅值归一化：
    // N * CG = sum(w)，因此预计算 1/sum(w) 和 2/sum(w)，循环内只做乘法。
    const double _2_PI = 2.0 * std::acos(-1.0);
    double windowSum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 * (1.0 - std::cos(_2_PI * i / (n - 1)));
        windowSum += w;
        raw.values[i] *= w;
    }

    fft_real(raw.values, raw.times, N);                         // 原地 FFT：复用 raw.times 作虚部，不额外分配内存

    const double invWindowSum = (windowSum > 0.0) ? (1.0 / windowSum) : 0.0;
    const double oneSidedScale = 2.0 * invWindowSum;            // 非 DC/Nyquist 频点的单边幅值系数

    out.channel = channel;
    out.times.resize(N / 2 + 1);                                // 标准单边谱包含 DC 到 Nyquist
    out.values.resize(N / 2 + 1);
    for (int i = 0; i <= N / 2; ++i) {
        out.times[i] = i * rate_N;
        const bool edgeBin = (i == 0 || i == N / 2);            // DC/Nyquist 不乘 2
        out.values[i] = raw.values[i] * (edgeBin ? invWindowSum : oneSidedScale);
    }
}

// ====== 导入导出 ======
void ChartManager::Private::exportData(const QString& fileName, double durationSeconds)
{
    // 按 durationSeconds 决定取数窗口：>0 取最近 N 秒，否则全量（-1 表示全部）
    double start = -1.0, end = -1.0;
    if (durationSeconds > 0.0 && m_latest > 0.0) {
        start = m_latest - durationSeconds;
        end   = m_latest;
    }
    emit dataExport(fileName, start, end, m_channelNames);   // 异步：DataExporter 在工作线程执行
}

void ChartManager::Private::do_exportFinished(bool success, const QString& message)
{
    emit m_manager->exportFinished(success, message);
}

void ChartManager::Private::importData(const QString& fileName)
{
    // 丢弃在途的并行刷新批次：导入完成后不再广播过期数据
    if (m_activeWatcher) {
        disconnect(m_activeWatcher, nullptr, this, nullptr);
        m_activeWatcher->future().cancel();        // 取消在途批次：排队任务不再执行，避免占线程池
        m_retiredWatchers.append(m_activeWatcher); // 任务仍会跑完，析构时统一等待，不能 deleteLater
        m_activeWatcher = nullptr;
    }
    isUpdating = false;                            // 清掉遗留标志，避免后续刷新被误丢弃
    isImporting = true;                            // 导入期间禁止任何刷新（含直接 updateData 调用）

    // 导入期间暂停定时刷新：避免读到半成品数据，完成后恢复
    m_timer->stop();

    // 导入前清空所有通道：旧缓冲不再有意义；时间轴重新锚定，视图跟随导入数据
    for (int ch = 0; ch < m_channelCount; ++ch)
        m_storage->resetData(ch);
    m_latest = -1.0;                               // 重置默认时间戳。
    emit dataImport(fileName);                     // 异步：DataImporter 在工作线程执行
}

void ChartManager::Private::do_importFinished(bool success, const QString& message)
{
    isImporting = false;
    m_timer->start();                              // 导入完成，恢复刷新
    emit m_manager->importFinished(success, message);
}

void ChartManager::Private::do_importNames(const QStringList& names)
{
    const int n = qMin(names.size(), m_channelCount);
    for (int ch = 0; ch < n; ++ch) {
        const QString name = names[ch].trimmed();
        if (name.isEmpty())  continue;            // 空列名不覆盖现有命名
        setChannelName(ch, name);                 // 更新图例名与导出列名
    }
}

// ====== 其他 ======
void ChartManager::Private::clearShow()
{
    for (ChartController *ctrl : controllers)
        ctrl->clearData();                  // 清数据不清图形：下轮刷新自动恢复
}

void ChartManager::Private::stopShow()
{
    // 停止显示：所有曲线不可见，含频谱/XY（数据保留；start/stop 控制刷新）
    for (ChartController *ctrl : controllers)
        ctrl->setViewVisible(false);
    m_forceRefresh = true;
    updateData();                           // 立即按隐藏状态重绘，无需等新数据
}

// ============================================================
// 简单原地 FFT：实数输入，结果以幅度谱写回原数组
// n 为 2 的幂（computeFFT 已保证），数据不足自动补零
// ============================================================
static void fft_real(QList<double>& re, QList<double>& im, int n)
{
    const int N = n;
    re.resize(N);                                  // 不足补零
    im.resize(N);
    im.fill(0.0);                                  // 虚部工作区（复用调用方数组，如 raw.times）

    // 1. 位反转重排
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1)  j ^= bit;
        j ^= bit;
        if (i < j)  qSwap(re[i], re[j]);
    }

    // 2. 蝶形运算
    const double _2_PI = 2.0 * std::acos(-1.0);
    for (int len = 2; len <= N; len <<= 1) {
        const double ang = -_2_PI / len;
        const double wRe = std::cos(ang), wIm = std::sin(ang);
        const int half = len >> 1;
        for (int i = 0; i < N; i += len) {
            double curRe = 1.0, curIm = 0.0;
            for (int k = 0; k < half; ++k) {
                const double uRe = re[i + k],        uIm = im[i + k];
                const double vRe = re[i + k + half] * curRe - im[i + k + half] * curIm;
                const double vIm = re[i + k + half] * curIm + im[i + k + half] * curRe;
                re[i + k]            = uRe + vRe;
                im[i + k]            = uIm + vIm;
                re[i + k + half]     = uRe - vRe;
                im[i + k + half]     = uIm - vIm;
                const double nRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nRe;
            }
        }
    }

    // 3. 幅度谱：实数输入后半段是前半段的镜像，只需算 [0, N/2]，再镜像补全
    re[0] = std::sqrt(re[0] * re[0] + im[0] * im[0]);          // DC bin
    for (int i = 1; i <= N / 2; ++i) {
        const double mag = std::sqrt(re[i] * re[i] + im[i] * im[i]);
        re[i]     = mag;
        re[N - i] = mag;                                       // 镜像 bin
    }
}

// ============================================================
// ChartManager 公共接口
// ============================================================
ChartManager::ChartManager(int channelCount, QObject *parent)
    : QObject(parent)
    , pimpl(new Private(channelCount, this)) {}

ChartManager::~ChartManager() = default;

void ChartManager::start()                       { pimpl->start(); }
void ChartManager::stop()                        { pimpl->stop(); }
void ChartManager::setPeriod(int ms)             { pimpl->setPeriod(ms); }
void ChartManager::setMode(ShowMode mode)        { pimpl->setMode(mode); }
ShowMode ChartManager::getMode() const           { return pimpl->getMode(); }

void ChartManager::addData(int channel, double value)                    { pimpl->addData(channel, value); }
void ChartManager::addData(int channel, double time, double value)       { pimpl->addData(channel, time, value); }
void ChartManager::addData(const QList<ChannelData>& dataList)           { pimpl->addData(dataList); }

int  ChartManager::createView(ViewType type)     { return pimpl->createView(type); }
void ChartManager::removeView(int viewIndex)     { pimpl->removeView(viewIndex); }
void ChartManager::attachChannel(int viewIndex, int channel)             { pimpl->attachChannel(viewIndex, channel); }
void ChartManager::detachChannel(int viewIndex, int channel)             { pimpl->detachChannel(viewIndex, channel); }
void ChartManager::setViewChannels(int viewIndex, const QList<int>& channels) { pimpl->setViewChannels(viewIndex, channels); }
QList<int> ChartManager::getViewChannels(int viewIndex) const            { return pimpl->getViewChannels(viewIndex); }
QWidget* ChartManager::getViewWidget(int viewIndex) const                { return pimpl->getViewWidget(viewIndex); }

void ChartManager::setChannelName(int channel, const QString& name)      { pimpl->setChannelName(channel, name); }
void ChartManager::setChannelColor(int channel, const QColor& color)     { pimpl->setChannelColor(channel, color); }
void ChartManager::setChannelVisible(int channel, bool visible)          { pimpl->setChannelVisible(channel, visible); }

void ChartManager::setViewRange(int viewIndex, double startTime, double endTime) { pimpl->setViewRange(viewIndex, startTime, endTime); }
void ChartManager::setWindowLen(double seconds)          { pimpl->setWindowLen(seconds); }
void ChartManager::setAbsTime(int viewIndex, bool enabled)               { pimpl->setAbsTime(viewIndex, enabled); }
void ChartManager::setBackColor(int viewIndex, int color)                { pimpl->setBackColor(viewIndex, color); }

ChannelData ChartManager::computeFFT(int channel, int nfft)              { return pimpl->computeFFT(channel, nfft); }
void ChartManager::exportData(const QString& fileName, double durationSeconds) { pimpl->exportData(fileName, durationSeconds); }
void ChartManager::importData(const QString& fileName)                   { pimpl->importData(fileName); }

void ChartManager::clearShow()                   { pimpl->clearShow(); }
void ChartManager::stopShow()                    { pimpl->stopShow(); }
