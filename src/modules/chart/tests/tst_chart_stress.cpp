// 压力测试：顶尖压力 = 60 通道 x 240000 点（14.4M 点）打满环形缓冲
//  1) 批量写入吞吐（ChartManager 公共接口）
//  2) 全量范围实时刷新 + 5ms 心跳（UI 线程最大卡顿；
//     刷新路径内部即"全量范围 + 默认 M4(2400) 降采样"）
//  3) 60 通道全量导出/导入
// 全部只依赖对外可见的 chart_manager.h。
#include "chart_manager.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
#include <cmath>

namespace {

const double kPi = std::acos(-1.0);
const int kChCount = 60;              // 最大通道数
const int kMaxPts  = 240000;          // 最大通道点数（环形缓冲容量）
const double kFs   = 1000.0;          // 采样率

double sineWave(int ch, int, double t)
{
    return std::sin(2.0 * kPi * 50.0 * t + ch * 0.21);
}

double noisySine(int ch, int i, double t)
{
    return sineWave(ch, i, t) * 100.0 + (i % 7) * 0.001;
}

QList<ChannelData> makeBatch(int chCount, int ptsPerCh, double fs,
                             double tStart, double (*wave)(int, int, double))
{
    QList<ChannelData> batch;
    batch.reserve(chCount);
    for (int ch = 0; ch < chCount; ++ch) {
        ChannelData cd;
        cd.channel = ch;
        cd.times.reserve(ptsPerCh);
        cd.values.reserve(ptsPerCh);
        for (int i = 0; i < ptsPerCh; ++i) {
            const double t = tStart + i / fs;
            cd.times.append(t);
            cd.values.append(wave(ch, i, t));
        }
        batch.append(cd);
    }
    return batch;
}

QStringList readLines(const QString &path)
{
    QFile f(path);
    QStringList lines;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return lines;
    while (!f.atEnd())
        lines.append(QString::fromUtf8(f.readLine()).trimmed());
    return lines;
}

int spectrumPeakBin(const ChannelData &spec)
{
    int peak = 0;
    for (int i = 1; i < spec.values.size(); ++i)
        if (spec.values[i] > spec.values[peak]) peak = i;
    return peak;
}

} // namespace

class ChartStressTest : public QObject
{
    Q_OBJECT
private slots:
    void batchWriteThroughput();
    void fullRangeRefreshStress();
    void exportImportStress();
};

void ChartStressTest::batchWriteThroughput()
{
    ChartManager m(kChCount);
    QElapsedTimer sw; sw.start();
    const QList<ChannelData> batch = makeBatch(kChCount, kMaxPts, kFs, 0.0, sineWave);
    const qint64 buildMs = sw.elapsed();
    sw.restart();
    m.addData(batch);                                  // 一次批量写入 14.4M 点
    const qint64 writeMs = sw.elapsed();

    const double totalM = kChCount * kMaxPts / 1e6;
    qInfo("写入: %d 通道 x %d 点 = %.2fM 点，构建 %lld ms，写入 %lld ms（%.1f M点/s）",
          kChCount, kMaxPts, totalM, buildMs, writeMs, totalM / (writeMs / 1000.0));
    QVERIFY2(writeMs < 60000,
             qPrintable(QStringLiteral("批量写入 14.4M 点不应超过 60s，实际 %1ms").arg(writeMs)));

    // 数据完整性：环形缓冲写满后，最新 1024 点的 50Hz 分量仍在
    const ChannelData spec = m.computeFFT(0, 1024);
    QCOMPARE(spec.times.size(), 512);
    const int peak = spectrumPeakBin(spec);
    QVERIFY2(qAbs(peak - 51) <= 4,
             qPrintable(QStringLiteral("写满后 FFT 峰值应仍为 50Hz(bin≈51)，实际 %1").arg(peak)));
}

void ChartStressTest::fullRangeRefreshStress()
{
    ChartManager m(kChCount);
    QElapsedTimer fill; fill.start();
    {
        const QList<ChannelData> batch = makeBatch(kChCount, kMaxPts, kFs, 0.0, sineWave);
        m.addData(batch);                              // 预填充 14.4M 点
    }
    qInfo("预填充: %d 通道 x %d 点（14.4M），%lld ms", kChCount, kMaxPts, fill.elapsed());

    const int vi = m.createView(View_Waveform);
    QList<int> chs;
    for (int ch = 0; ch < kChCount; ++ch) chs << ch;
    m.setViewChannels(vi, chs);                        // 60 条曲线
    m.setViewRange(vi, 0.0, 250.0);                    // 全量范围手动视图
    m.setPeriod(20);

    // 5ms 心跳：UI 线程被重绘/广播卡住时心跳间隔会显著拉长
    int beats = 0;
    qint64 lastBeatMs = 0, maxGapMs = 0;
    QElapsedTimer beatClock; beatClock.start();
    QTimer heartbeat;
    heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        const qint64 now = beatClock.elapsed();
        maxGapMs = qMax(maxGapMs, now - lastBeatMs);
        lastBeatMs = now;
        ++beats;
    });
    heartbeat.start();

    m.start();
    QElapsedTimer run; run.start();
    int frame = 0;
    const int perFrame = 4;
    for (; run.elapsed() < 6000; ++frame) {
        const double tStart = 240.0 + frame * perFrame / kFs;
        m.addData(makeBatch(kChCount, perFrame, kFs, tStart, noisySine));
        QTest::qWait(5);                               // 让刷新周期处理
    }
    m.stop();
    heartbeat.stop();

    qInfo("全量刷新: 6s 推送 %d 帧 x %d 通道 x %d 点；心跳 %d 次，最大间隔 %lld ms",
          frame, kChCount, perFrame, beats, maxGapMs);
    QVERIFY2(maxGapMs < 300,
             qPrintable(QStringLiteral("UI 线程被拖慢：最大心跳间隔 %1ms（历史重绘卡顿为 870ms+）").arg(maxGapMs)));

    // 尾部完整性：导出最近 2s 窗口（证明刷新跑通、环形缓冲尾部数据完好）
    QSignalSpy spy(&m, &ChartManager::exportFinished);
    const QString path = QDir::temp().filePath(QStringLiteral("chart_stress_fullrange.csv"));
    m.exportData(path, 2.0);
    QVERIFY(spy.wait(60000));
    QVERIFY(spy.takeFirst().at(0).toBool());
    const QStringList lines = readLines(path);
    qInfo("尾部 2s 窗口导出: %d 行（含 3 行头）", lines.size());
    QVERIFY(lines.size() >= 1995 && lines.size() <= 2010);
    QCOMPARE(lines[2].count(','), kChCount);
    QFile::remove(path);
}

void ChartStressTest::exportImportStress()
{
    ChartManager m(kChCount);
    const int pts = 60000;
    m.addData(makeBatch(kChCount, pts, kFs, 0.0, sineWave));  // 3.6M 点

    const QString path = QDir::temp().filePath(QStringLiteral("chart_stress_export.csv"));
    QSignalSpy spy(&m, &ChartManager::exportFinished);
    QElapsedTimer sw; sw.start();
    m.exportData(path, -1.0);
    QVERIFY(spy.wait(120000));
    QVERIFY(spy.takeFirst().at(0).toBool());
    const qint64 exportMs = sw.elapsed();
    const qint64 bytes = QFileInfo(path).size();
    const double exportSec = qMax(1.0, exportMs / 1000.0);
    qInfo("导出: %d 通道 x %d 点，%lld 字节，%lld ms（%.1f MB/s）",
          kChCount, pts, bytes, exportMs, bytes / 1048576.0 / exportSec);
    QVERIFY2(exportMs < 120000,
             qPrintable(QStringLiteral("全量导出不应超过 120s，实际 %1ms").arg(exportMs)));

    const QStringList lines = readLines(path);
    QCOMPARE(lines.size(), pts + 3);                   // 60000 行数据 + 3 行头
    QCOMPARE(lines[2].count(','), kChCount);

    // 导入新 manager
    ChartManager b(kChCount);
    QSignalSpy spyImp(&b, &ChartManager::importFinished);
    QElapsedTimer sw2; sw2.start();
    b.importData(path);
    QVERIFY(spyImp.wait(180000));
    QVERIFY(spyImp.takeFirst().at(0).toBool());
    const qint64 importMs = sw2.elapsed();
    const double importSec = qMax(1.0, importMs / 1000.0);
    qInfo("导入: %lld ms（%.1f MB/s）", importMs, bytes / 1048576.0 / importSec);
    QVERIFY2(importMs < 180000,
             qPrintable(QStringLiteral("全量导入不应超过 180s，实际 %1ms").arg(importMs)));

    QFile::remove(path);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");           // 无窗口运行（CI/命令行友好）
    QApplication app(argc, argv);
    ChartStressTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_chart_stress.moc"
