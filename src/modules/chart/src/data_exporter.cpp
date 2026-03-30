#include <QFile>
#include <QSet>
#include <QMap>
#include <QDateTime>
#include "data_exporter.h"
#include "data_storage.h"

void DataExporter::do_dataExport(const QString& fileName, qint64 startTime, qint64 endTime)
{
    QFile file(fileName);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        emit exportFinished(false, QString("无法打开文件：%1").arg(file.errorString()));
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    //获取所有通道的数据
    int count = m_storage->getChannelCount();           //获取通道数
    QList<QList<qint64>> timestamps(count);
    QList<QList<float>> targets(count), actuals(count);
    for(int ch = 0; ch < count; ch++)
    {
        m_storage->getData(ch, startTime, endTime, targets[ch], actuals[ch], timestamps[ch]);
    }

    //合并所有时间戳
    QSet<qint64> tsSet;
    for(int ch = 0; ch < count; ch++)
    {
        tsSet.unite(QSet<qint64>(timestamps[ch].begin(), timestamps[ch].end()));
    }
    QList<qint64> mergedTimestamps = tsSet.values();
    std::sort(mergedTimestamps.begin(), mergedTimestamps.end());    //排序

    if(mergedTimestamps.isEmpty())
    {
        emit exportFinished(false, "没有可以导出的数据");
        return;
    }

    // 计算最早时间戳和总时长（秒）
    qint64 minTimestamp = mergedTimestamps.first();
    qint64 maxTimestamp = mergedTimestamps.last();
    double totalSeconds = (maxTimestamp - minTimestamp) / 1000.0;

    // 写入元数据注释行
    out << "# LTM_Monitor 上位机监控调试软件 (Lvtou CSU)" << "\n";
    if(minTimestamp != 0)    
    {   //最早基准时间戳如果为0，说明导入的是相对时间戳，已经丢失时间信息了
        QDateTime minDt = QDateTime::fromMSecsSinceEpoch(minTimestamp);
        QString minAbsTimeStr = minDt.toString("yyyy/MM/dd hh:mm:ss");
        out << "# StartTime: " << minAbsTimeStr << "\n";
    }
    out << "# Duration(sec): " << QString::number(totalSeconds, 'f', 1) << "\n";

    //构建时间戳到值的映射，方便快速查找
    QList<QMap<qint64, float>> targetMaps(count), actualMaps(count);
    for(int ch = 0; ch < count; ch++)
    {
        for(int i = 0; i < timestamps[ch].size(); i++)
        {
            if(qAbs(targets[ch][i]) < 1e-8) targets[ch][i] = 0;
            if(qAbs(actuals[ch][i]) < 1e-8) actuals[ch][i] = 0; 
            targetMaps[ch].insert(timestamps[ch][i], targets[ch][i]);
            actualMaps[ch].insert(timestamps[ch][i], actuals[ch][i]);
        }
    }

     // 写入CSV头
    out << "Timestamp_ms";
    for (int ch = 0; ch < count; ++ch) {
        out << ",Ch" << ch+1 << "_Target,Ch" << ch+1 << "_Actual";
    }
    out << "\n";

    // 写入数据行
    for (qint64 t : mergedTimestamps) {
        out << (t - minTimestamp);
        for (int ch = 0; ch < count; ++ch) {
            out << ",";
            if (targetMaps[ch].contains(t))
                out << targetMaps[ch][t];
            out << ",";
            if (actualMaps[ch].contains(t))
                out << actualMaps[ch][t];
        }
        out << "\n";
    }

    file.close();  //记得关闭文件
    emit exportFinished(true, QString("导出成功，共 %1 行数据").arg(mergedTimestamps.size()));
}

