#include "chart_manager_private.h"
#include "chart_controller.h"
#include "data_storage.h"
#include "data_importer.h"
#include "data_exporter.h"
#include "chart_def.h"

#include <QtConcurrent>
#include <QThread>
#include <cmath>

// 简单原地 FFT：实数输入 -> 幅度谱（定义见文件末尾，公共接口之前）
static void fft_real(QList<double>& re, QList<double>& im, int n);   // im 为调用方提供的虚部工作区（如 raw.times）

// ============================================================
// ChartManager::Private：模型(DataStorage) + 视图集合(ChartController) 的调度层
// 数据写入委托给 DataStorage；显示由各 ChartController 负责。
// ============================================================

ChartManager::Private::Private(int channelCount, ChartManager *parent)
    : QObject(parent), m_manager(parent), m_channelCount(channelCount)
{
    m_channelNames.fill(QString(), m_channelCount);        // 通道名初始为空，导出时回退 ChN
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

    const int idx = controllers.size();
    connect(ctrl, &ChartController::rangeChanged, this, [this, idx](double min, double max) {
        if (idx < viewRanges.size()) {
            viewRanges[idx]     = QPair<double,double>(min, max);
            viewAutoFollow[idx] = false;            // 用户拖拽/缩放 -> 切手动
        }
        m_forceRefresh = true;   // 用户拖拽/缩放：强制按新范围重绘
        updateData();
    });

    controllers.append(ctrl);
    viewChannels.append(QList<int>());
    viewTypes.append(type);
    viewRanges.append(QPair<double,double>(0.0, m_windowLen));
    viewAutoFollow.append(true);
    viewAbsTime.append(false);
    viewBackColor.append(0);
    ctrl->setBackColor(Qt::white);
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
    viewChannels.removeAt(viewIndex);
    viewTypes.removeAt(viewIndex);
    viewRanges.removeAt(viewIndex);
    viewAutoFollow.removeAt(viewIndex);
    viewAbsTime.removeAt(viewIndex);
    viewBackColor.removeAt(viewIndex);
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
    if (viewChannels[viewIndex].contains(channel))          return;
    viewChannels[viewIndex].append(channel);
    controllers[viewIndex]->addChannel(channel, m_channelNames[channel]);
    if (viewTypes[viewIndex] == View_XY)
        controllers[viewIndex]->setXYChannels(viewChannels[viewIndex]);
    m_forceRefresh = true;
    updateData();
}

void ChartManager::Private::detachChannel(int viewIndex, int channel)
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return;
    viewChannels[viewIndex].removeAll(channel);
    controllers[viewIndex]->removeChannel(channel);
    if (viewTypes[viewIndex] == View_XY)
        controllers[viewIndex]->setXYChannels(viewChannels[viewIndex]);
    m_forceRefresh = true;
    updateData();
}

void ChartManager::Private::setViewChannels(int viewIndex, const QList<int>& channels)
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return;
    viewChannels[viewIndex].clear();
    for (int ch : channels) {
        if (ch >= 0 && ch < m_channelCount)
            viewChannels[viewIndex].append(ch);
    }
    controllers[viewIndex]->setChannels(viewChannels[viewIndex]);
    for (int ch : viewChannels[viewIndex])
        controllers[viewIndex]->setChannelName(ch, m_channelNames[ch]);   // 新建曲线也带当前通道名
    m_forceRefresh = true;
    updateData();
}

QList<int> ChartManager::Private::getViewChannels(int viewIndex) const
{
    if (viewIndex < 0 || viewIndex >= viewChannels.size()) return QList<int>();
    return viewChannels[viewIndex];
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
    for (ChartController *ctrl : controllers)
        ctrl->setChannelVisible(channel, visible);
    m_forceRefresh = true;   // 可见性变化：下个刷新周期重绘
}

// ====== 坐标轴 ======
void ChartManager::Private::setViewRange(int viewIndex, double startTime, double endTime)
{
    if (viewIndex < 0 || viewIndex >= controllers.size()) return;
    viewRanges[viewIndex]     = QPair<double,double>(startTime, endTime);
    viewAutoFollow[viewIndex] = false;
    controllers[viewIndex]->setXRange(startTime, endTime);
    m_forceRefresh = true;
    updateData();
}

void ChartManager::Private::setAbsTime(int viewIndex, bool enabled)
{
    if (viewIndex < 0 || viewIndex >= viewAbsTime.size()) return;
    viewAbsTime[viewIndex] = enabled;
    m_forceRefresh = true;
    updateData();
}

void ChartManager::Private::setBackColor(int viewIndex, int color)
{
    if (viewIndex < 0 || viewIndex >= viewBackColor.size()) return;
    viewBackColor[viewIndex] = color;
    controllers[viewIndex]->setBackColor(color == 0 ? Qt::white : Qt::black);
    m_forceRefresh = true;   // 外观变化：下个刷新周期重绘
}

// ====== 每轮更新：窗口 -> 去重取数（异步） -> 广播 ======
void ChartManager::Private::updateData()
{
    if (isUpdating || isImporting) return;        // 上一批未完成或导入中，跳过（并发保护）
    isUpdating = true;

    // 1. 可见通道去重 + 频谱通道集合（频谱视图绑定的通道）
    QList<int> channels;
    QList<int> fftChannels;
    for (int vi = 0; vi < viewChannels.size(); ++vi) {
        const bool isSpec = (viewTypes[vi] == View_Spectrum || viewTypes[vi] == View_WaveformSpectrum);
        for (int ch : viewChannels[vi]) {
            if (!channels.contains(ch))
                channels.append(ch);
            if (isSpec && !fftChannels.contains(ch))
                fftChannels.append(ch);
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

    // 2. 全局窗口：优先手动视图范围，否则自动跟随最新
    double start = -1.0, end = -1.0;            // <0 表示取全部
    for (int i = 0; i < viewRanges.size(); ++i) {
        if (!viewAutoFollow[i]) {
            start = viewRanges[i].first;
            end   = viewRanges[i].second;
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

    // 3. 启动异步并行取数（线程池执行，主线程不被阻塞）
    const double winStart = start, winEnd = end;
    QFuture<ParallelResult> future = QtConcurrent::mapped(
        channels, [this, winStart, winEnd, fftChannels](int ch) -> ParallelResult {
            ParallelResult res;
            res.data     = m_storage->getData(ch, winStart, winEnd, m_threshold, /*isLTTB*/ false);
            res.spectrum = nullptr;
            if (fftChannels.contains(ch)) {
                computeFFTInto(m_fftBufs[ch], ch, m_nfft);   // 频谱计算也在并行任务中
                res.spectrum = &m_fftBufs[ch];
            }
            return res;
        });

    // 4. QFutureWatcher 异步收尾：完成后再广播，主线程零阻塞
    QFutureWatcher<ParallelResult> *watcher = new QFutureWatcher<ParallelResult>(this);
    m_activeWatcher = watcher;
    connect(watcher, &QFutureWatcher<ParallelResult>::finished, this, [this, watcher]() {
        broadcastResults(watcher->future().results());   // finished 后 results() 不阻塞
        watcher->deleteLater();
        if (m_activeWatcher == watcher) m_activeWatcher = nullptr;
    });
    watcher->setFuture(future);
}

// ====== 结果就绪（主线程）：零拷贝广播到所有订阅视图 ======
void ChartManager::Private::broadcastResults(const QList<ParallelResult>& results)
{
    QHash<int, QList<double>> shiftedX;      // 相对时间：每通道只平移一次，多视图共享

    for (const ParallelResult &r : results) {
        ChannelData *data = r.data;
        if (!data || data->times.isEmpty())   continue;

        // 时间戳单调递增：last 即该通道最新时间，下标访问即可得范围
        m_latest = qMax(m_latest, data->times.last());

        // ChannelData 自带 channel，直接按它广播，无需哈希
        for (int vi = 0; vi < controllers.size(); ++vi) {
            if (viewTypes[vi] == View_Spectrum)   continue;   // 纯频谱视图只收频谱，不画波形
            if (!viewChannels[vi].contains(data->channel))   continue;
            ChartController *ctrl = controllers[vi];
            if (!viewAbsTime[vi]) {
                // 相对时间：同一通道的平移 X 只算一次，其余视图直接复用
                QList<double> &x = shiftedX[data->channel];
                if (x.isEmpty()) {
                    x = data->times;                          // 共享缓冲不能就地改，拷贝一份
                    double maxX = x.last();
                    for (double &t : x) t -= maxX;
                }
                ctrl->updateData(data->channel, x, data->values);
            } else {
                // 绝对时间：直接传内部缓冲（隐式共享，零拷贝）
                ctrl->updateData(data->channel, data->times, data->values);
            }
        }
    }

    // 频谱视图：取绑定通道的频谱缓冲，零拷贝广播（多视图共享同一份）
    for (const ParallelResult &r : results) {
        if (!r.spectrum || r.spectrum->times.isEmpty())   continue;
        const int ch = r.spectrum->channel;
        for (int vi = 0; vi < controllers.size(); ++vi) {
            if (viewTypes[vi] != View_Spectrum && viewTypes[vi] != View_WaveformSpectrum)  continue;
            if (!viewChannels[vi].contains(ch))   continue;
            controllers[vi]->updateSpectrum(r.spectrum->times, r.spectrum->values);
        }
    }

    for (ChartController *ctrl : controllers)
        ctrl->replotQueued();

    isUpdating = false;
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

    // 汉宁窗：抑制频谱泄漏；相干增益 0.5，乘 2 补偿，峰值幅度与矩形窗一致
    const double _2_PI = 2.0 * std::acos(-1.0);
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 * (1.0 - std::cos(_2_PI * i / (n - 1)));
        raw.values[i] *= w * 2.0;
    }

    fft_real(raw.values, raw.times, N);                         // 原地 FFT：复用 raw.times 作虚部，不额外分配内存

    out.channel = channel;
    out.times.resize(N / 2);
    out.values.resize(N / 2);
    for (int i = 0; i < N / 2; ++i) {
        out.times[i]  = i * rate_N;
        out.values[i] = raw.values[i];                          // 线性幅值（无参考幅值，不做 dB）
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
void ChartManager::setAbsTime(int viewIndex, bool enabled)               { pimpl->setAbsTime(viewIndex, enabled); }
void ChartManager::setBackColor(int viewIndex, int color)                { pimpl->setBackColor(viewIndex, color); }

ChannelData ChartManager::computeFFT(int channel, int nfft)              { return pimpl->computeFFT(channel, nfft); }
void ChartManager::exportData(const QString& fileName, double durationSeconds) { pimpl->exportData(fileName, durationSeconds); }
void ChartManager::importData(const QString& fileName)                   { pimpl->importData(fileName); }

void ChartManager::clearShow()                   { pimpl->clearShow(); }
void ChartManager::stopShow()                    { pimpl->stopShow(); }
