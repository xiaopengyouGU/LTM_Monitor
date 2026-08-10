#include <QFile>
#include <QVector>
#include <QTextStream>
#include <QDateTime>
#include <cmath>
#include "data_exporter.h"
#include "data_storage.h"
#include "chart_manager.h"

// 最多 decimals 位小数，自动抹零（去掉末尾 0 与小数点）
static QString fmtTrim(double v, int decimals)
{
    QString s = QString::number(v, 'f', decimals);
    while (s.contains('.') && s.endsWith('0'))
        s.chop(1);
    if (s.endsWith('.'))
        s.chop(1);
    return s;
}

// CSV 字段转义：含逗号/引号/换行/回车时用双引号包裹，内部引号翻倍
static QString csvField(const QString& s)
{
    if (!s.contains(',') && !s.contains('"') && !s.contains('\n') && !s.contains('\r'))
        return s;
    QString e = s;
    e.replace('"', "\"\"");
    return QString("\"%1\"").arg(e);
}

void DataExporter::do_dataExport(const QString& fileName, double startTime, double endTime, const QStringList& nameList)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportFinished(false, QString("无法打开文件：%1").arg(file.errorString()));
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    const int count = nameList.size();
    QList<ChannelData> datas;
    datas.reserve(count);
    double tMin = 0.0, tMax = 0.0;
    bool have = false;                  // 当前是否有数据
    for (int ch = 0; ch < count; ++ch) {
        ChannelData raw = m_storage->getData(ch, startTime, endTime);   // 按窗口取数，不整段拷贝
        const QList<double>& times = raw.times;
        if (!times.isEmpty()) {
            if (!have) { tMin = times.first(); tMax = times.last(); have = true; }
            else       { tMin = qMin(tMin, times.first()); tMax = qMax(tMax, times.last()); }
        }
        datas.append(raw);
    }
    if (!have) { emit exportFinished(false, "没有可以导出的数据"); return; }

    // 时间戳按 4 位小数取整（0.0001s）作为合并键，避免浮点键碰撞
    auto round4 = [](double t) { return std::round(t * 10000.0) / 10000.0; };

    // 各通道时间戳升序：多路归并流式写行，不攒整张表（内存恒定，只扫一遍）
    int rowCnt = 0;
    QVector<int> idx(count, 0);
    while (1) {
        double minKey = 0.0;
        bool any = false;
        for (int ch = 0; ch < count; ++ch) {
            if (idx[ch] >= datas[ch].times.size())  continue;
            const double k = round4(datas[ch].times[idx[ch]]);
            if (!any || k < minKey) { minKey = k; any = true; }
        }
        if (!any) break;

        out << fmtTrim(minKey, 4);                 // 时间戳最多 4 位小数
        for (int ch = 0; ch < count; ++ch) {
            int idx_ch = idx[ch];
            const QList<double>& times  = datas[ch].times;
            const QList<double>& values = datas[ch].values;
            if (idx_ch >= times.size())          { out << ","; continue; }
            if (round4(times[idx_ch]) != minKey) { out << ","; continue; }
            // 同一通道同一键可能多点（间隔 < 0.0001s），取最后一个
            double v = values[idx_ch];
            while (idx_ch + 1 < times.size() &&
                   round4(times[idx_ch + 1]) == minKey) {
                ++idx_ch;
                v = values[idx_ch];
            }
            out << "," << fmtTrim(v, 6);           // 数值最多 6 位小数，自动抹零
            idx[ch] = idx_ch + 1;
        }
        out << "\n";
        ++rowCnt;
    }
    if (rowCnt == 0) { emit exportFinished(false, "没有可以导出的数据"); return; }

    const double totalSeconds = tMax - tMin;                // 导出数据时长（s）

    // 首行：标题
    out << "LTM_Monitor 监控调试上位机！！！\n";
    // 第二行：导出日期 + 数据时长
    out << QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss")
        << ", 数据时长: " << fmtTrim(totalSeconds, 1) << " s\n";
    // 表头：time_s + 各通道名（空名回退 ChN）
    out << "time_s";
    for (int ch = 0; ch < count; ++ch) {
        QString name = nameList[ch].trimmed();
        if (name.isEmpty())   name = QString("Ch%1").arg(ch);
        out << "," << csvField(name);
    }
    out << "\n";

    file.close();
    emit exportFinished(true, QString("导出成功，共 %1 行数据").arg(rowCnt));
}
