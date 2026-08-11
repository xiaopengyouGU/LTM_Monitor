// 公共接口单元测试：只依赖 chart_manager.h（外部用户能看到的唯一入口）
// 覆盖：默认配色、视图生命周期、模式/通道控制、FFT 正确性、导出导入往返。
#include "chart_manager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>
#include <cmath>

namespace {

const double kPi = std::acos(-1.0);

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

} // namespace

class ChartApiTest : public QObject
{
    Q_OBJECT
private slots:
    void defaultColorCycles();
    void viewLifecycle();
    void modesAndChannels();
    void fftOfSineWave();
    void exportImportRoundTrip();
    void exportFailurePath();
};

void ChartApiTest::defaultColorCycles()
{
    QSet<QRgb> first20;
    for (int ch = 0; ch < 20; ++ch) {
        const QColor c = getChannelColor(ch);
        QVERIFY(c.isValid());
        first20.insert(c.rgba());
    }
    QCOMPARE(first20.size(), 20);                        // 前 20 通道颜色互不相同
    QCOMPARE(getChannelColor(0),  getChannelColor(20));  // 超过 20 后按通道号循环
    QCOMPARE(getChannelColor(13), getChannelColor(33));
    QVERIFY(getChannelColor(-1).isValid());              // 非法通道回退黑色
}

void ChartApiTest::viewLifecycle()
{
    ChartManager m(8);

    const int vi = m.createView(View_Waveform);
    QCOMPARE(vi, 0);
    QVERIFY(m.getViewWidget(vi) != nullptr);
    QVERIFY(m.getViewChannels(vi).isEmpty());

    m.attachChannel(vi, 2);
    m.attachChannel(vi, 5);
    m.attachChannel(vi, 2);                              // 重复绑定应被忽略
    QCOMPARE(m.getViewChannels(vi).size(), 2);
    QCOMPARE(m.getViewChannels(vi).at(0), 2);
    QCOMPARE(m.getViewChannels(vi).at(1), 5);

    m.detachChannel(vi, 2);
    QCOMPARE(m.getViewChannels(vi).size(), 1);
    QCOMPARE(m.getViewChannels(vi).at(0), 5);

    m.setViewChannels(vi, {0, 1, 2, 3});
    QCOMPARE(m.getViewChannels(vi).size(), 4);
    QCOMPARE(m.getViewChannels(vi).at(3), 3);

    // 非法索引/通道号：安全返回，不崩溃
    QVERIFY(m.getViewWidget(99) == nullptr);
    QVERIFY(m.getViewChannels(99).isEmpty());
    m.attachChannel(99, 1);
    m.attachChannel(vi, 99);
    m.detachChannel(99, 1);
    m.removeView(99);

    // 其余视图类型都能创建
    const int vs  = m.createView(View_Spectrum);
    const int vxy = m.createView(View_XY);
    const int vws = m.createView(View_WaveformSpectrum);
    QVERIFY(m.getViewWidget(vs)  != nullptr);
    QVERIFY(m.getViewWidget(vxy) != nullptr);
    QVERIFY(m.getViewWidget(vws) != nullptr);

    m.removeView(-1);                                    // 清空所有视图
    QVERIFY(m.getViewWidget(0) == nullptr);
}

void ChartApiTest::modesAndChannels()
{
    ChartManager m(4);
    QCOMPARE(int(m.getMode()), int(Mode_Auto));

    const int vi = m.createView(View_Waveform);
    m.setViewChannels(vi, {0, 1});
    m.setChannelName(1, QStringLiteral("电压"));
    m.setChannelColor(1, QColor(12, 34, 56));
    m.setChannelVisible(1, false);

    m.setMode(Mode_Hand);
    QCOMPARE(int(m.getMode()), int(Mode_Hand));
    m.setViewRange(vi, 1.0, 5.0);                        // 手动设范围后内部切手动
    m.setAbsTime(vi, true);
    m.setBackColor(vi, 1);                               // 黑底

    m.setMode(Mode_Auto);
    QCOMPARE(int(m.getMode()), int(Mode_Auto));

    m.stopShow();                                        // 隐藏所有曲线，数据保留
    m.clearShow();                                       // 清空显示数据
}

void ChartApiTest::fftOfSineWave()
{
    ChartManager m(4);
    const double fs = 1000.0;                            // 1kHz 采样
    const int n = 2048;
    for (int i = 0; i < n; ++i) {
        const double t = i / fs;
        m.addData(0, t, std::sin(2.0 * kPi * 50.0 * t)); // 50Hz 正弦
    }

    const ChannelData spec = m.computeFFT(0, 1024);
    QCOMPARE(spec.times.size(), 512);                    // N/2 个频点
    QCOMPARE(spec.times.size(), spec.values.size());
    for (int i = 1; i < spec.times.size(); ++i)
        QVERIFY2(spec.times[i] > spec.times[i - 1], "频率轴必须严格递增");
    for (double v : spec.values)
        QVERIFY(v >= 0.0);                               // 幅度非负

    int peak = 0;
    for (int i = 1; i < spec.values.size(); ++i)
        if (spec.values[i] > spec.values[peak]) peak = i;
    // fs/N = 0.9766Hz/bin，50Hz 应落在 bin≈51
    QVERIFY2(qAbs(peak - 51) <= 4,
             qPrintable(QStringLiteral("50Hz 峰值应落在 bin≈51，实际 %1").arg(peak)));
    QVERIFY(spec.values[peak] > 100.0);                  // 幅度量级 ≈ N/2
    QVERIFY(spec.values[0] < spec.values[peak] / 2.0);   // DC 被汉宁窗压住
}

void ChartApiTest::exportImportRoundTrip()
{
    const QString path1 = QDir::temp().filePath(QStringLiteral("chart_rt_a.csv"));
    const QString path2 = QDir::temp().filePath(QStringLiteral("chart_rt_b.csv"));

    ChartManager a(4);
    const int n = 3000;
    for (int i = 0; i < n; ++i) {
        const double t0 = i * 0.001;
        a.addData(0, t0, std::sin(t0 * 100.0) + i * 1e-6);
        const double t2 = i * 0.001 + 0.0003;            // 时间轴与 ch0 错开
        a.addData(2, t2, std::cos(t2 * 80.0));
        // ch1、ch3 保持无数据：导出必须跳过空通道
    }

    QSignalSpy spy(&a, &ChartManager::exportFinished);
    a.exportData(path1, -1.0);
    QVERIFY(spy.wait(15000));
    QVERIFY(spy.takeFirst().at(0).toBool());

    const QStringList lines1 = readLines(path1);
    QVERIFY(lines1.size() > 3);
    QVERIFY(lines1[2].startsWith(QStringLiteral("time_s")));
    QCOMPARE(lines1[2].count(','), 2);                   // 只有 Ch0、Ch2 两列
    QVERIFY(lines1[2].contains(QStringLiteral("Ch0")));
    QVERIFY(lines1[2].contains(QStringLiteral("Ch2")));
    QVERIFY(!lines1[2].contains(QStringLiteral("Ch1"))); // 空通道不导出

    // 导入新 manager，再导出：表头+数据应与第一次完全一致（第 2 行日期除外）
    ChartManager b(4);
    QSignalSpy spyImp(&b, &ChartManager::importFinished);
    b.importData(path1);
    QVERIFY(spyImp.wait(15000));
    QVERIFY(spyImp.takeFirst().at(0).toBool());

    QSignalSpy spy2(&b, &ChartManager::exportFinished);
    b.exportData(path2, -1.0);
    QVERIFY(spy2.wait(15000));
    QVERIFY(spy2.takeFirst().at(0).toBool());

    const QStringList lines2 = readLines(path2);
    QCOMPARE(lines1.size(), lines2.size());
    for (int i = 2; i < lines1.size(); ++i)
        QCOMPARE(lines1[i], lines2[i]);                  // 表头+数据逐行一致

    QFile::remove(path1);
    QFile::remove(path2);
}

void ChartApiTest::exportFailurePath()
{
    ChartManager m(2);
    m.addData(0, 0.0, 1.0);                              // 至少有数据可导
    QSignalSpy spy(&m, &ChartManager::exportFinished);
    m.exportData(QStringLiteral("C:/__chart_no_such_dir_xyz__/a.csv"), -1.0);
    QVERIFY(spy.wait(15000));
    QCOMPARE(spy.takeFirst().at(0).toBool(), false);     // 打不开文件 → 失败信号
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");             // 无窗口运行（CI/命令行友好）
    QApplication app(argc, argv);
    ChartApiTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_chart_api.moc"
