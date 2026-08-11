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
    QList<ChannelData> datas;               // 只有数据的通道（导出列）
    QList<int> chIndexes;                   // 导出列 -> 原通道号（写表头用）
    datas.reserve(count);
    double tMin = 0.0, tMax = 0.0;
    for (int ch = 0; ch < count; ++ch) {
        ChannelData raw = m_storage->getData(ch, startTime, endTime);   // 按窗口取数，不整段拷贝
        if (raw.times.isEmpty())  continue;                             // 无数据通道直接略过
        bool allNaN = true;                                             // 全 NaN 视为无数据（兜底）
        for (double v : raw.values)
            if (!std::isnan(v)) { allNaN = false; break; }
        if (allNaN)  continue;
        if (datas.isEmpty())      { tMin = raw.times.first(); tMax = raw.times.last(); }
        else                      { tMin = qMin(tMin, raw.times.first()); tMax = qMax(tMax, raw.times.last()); }
        chIndexes.append(ch);
        datas.append(raw);
    }
    if (datas.isEmpty()) { emit exportFinished(false, "没有可以导出的数据"); return; }

    // 首行：标题
    out << "LTM_Monitor 监控调试上位机！！！\n";
    // 第二行：导出日期 + 数据时长
    const double totalSeconds = tMax - tMin;                // 导出数据时长（s）
    out << QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss")
        << ", 数据时长: " << fmtTrim(totalSeconds, 1) << " s\n";
    // 表头：time_s + 各通道名（空名回退 ChN）
    out << "time_s";
    for (int ci = 0; ci < datas.size(); ++ci) {
        const int ch = chIndexes[ci];
        QString name = nameList[ch].trimmed();
        if (name.isEmpty())   name = QString("Ch%1").arg(ch);
        out << "," << csvField(name);
    }
    out << "\n";

    // 时间戳按 4 位小数取整（0.0001s）作为合并键，避免浮点键碰撞
    auto round4 = [](double t) { return std::round(t * 10000.0) / 10000.0; };

    // 时间轴偏移：导出从 0 开始（窗口起始时间不一定为 0）
    auto keyOf = [&](double t) { return round4(t - tMin); };

    // 各通道时间戳升序：多路归并流式写行，不攒整张表（内存恒定，只扫一遍）
    const int colCount = datas.size();
    int rowCnt = 0;
    QVector<int> idx(colCount, 0);
    while (1) {
        double minKey = 0.0;
        bool any = false;
        for (int ci = 0; ci < colCount; ++ci) {
            if (idx[ci] >= datas[ci].times.size())  continue;
            const double k = keyOf(datas[ci].times[idx[ci]]);
            if (!any || k < minKey) { minKey = k; any = true; }
        }
        if (!any) break;

        out << fmtTrim(minKey, 4);                 // 时间戳最多 4 位小数
        for (int ci = 0; ci < colCount; ++ci) {
            int idx_ch = idx[ci];
            const QList<double>& times  = datas[ci].times;
            const QList<double>& values = datas[ci].values;
            if (idx_ch >= times.size())          { out << ","; continue; }
            if (keyOf(times[idx_ch]) != minKey)  { out << ","; continue; }
            // 同一通道同一键可能多点（间隔 < 0.0001s），取最后一个
            double v = values[idx_ch];
            while (idx_ch + 1 < times.size() &&
                   keyOf(times[idx_ch + 1]) == minKey) {
                ++idx_ch;
                v = values[idx_ch];
            }
            out << "," << fmtTrim(v, 6);           // 数值最多 6 位小数，自动抹零
            idx[ci] = idx_ch + 1;
        }
        out << "\n";
        ++rowCnt;
    }

    file.close();
    emit exportFinished(true, QString("导出成功，共 %1 行数据").arg(rowCnt));
}
