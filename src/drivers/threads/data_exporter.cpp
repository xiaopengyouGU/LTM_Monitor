#include "data_exporter.h"

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
    QList<QList<qint64>> timestamps(5);
    QList<QList<float>> targets(5), actuals(5);
    for(int ch = 0; ch < 5; ch++)
    {
        m_storage->getData(ch, startTime, endTime, targets[ch], actuals[ch], timestamps[ch]);
    }
    //合并所有时间戳
    QSet<qint64> tsSet;
    for(int ch = 0; ch < 5; ch++)
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

    //构建时间戳到值的映射，方便快速查找
    QList<QMap<qint64, float>> targetMaps(5), actualMaps(5);
    for(int ch = 0; ch < 5; ch++)
    {
        for(int i = 0; i < timestamps[ch].size(); i++)
        {
            targetMaps[ch].insert(timestamps[ch][i], targets[ch][i]);
            actualMaps[ch].insert(timestamps[ch][i], actuals[ch][i]);
        }
    }

     // 写入CSV头
    out << "Timestamp_ms";
    for (int ch = 0; ch < 5; ++ch) {
        out << ",Ch" << ch+1 << "_Target,Ch" << ch+1 << "_Actual";
    }
    out << "\n";

    // 写入数据行
    for (qint64 t : mergedTimestamps) {
        out << t;
        for (int ch = 0; ch < 5; ++ch) {
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

