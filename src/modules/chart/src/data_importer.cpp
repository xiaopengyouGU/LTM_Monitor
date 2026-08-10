#include "data_importer.h"
#include "data_storage.h"
#include "chart_manager.h"   // ChannelData 完整定义
#include <QTextStream>
#include <QFile>

// 解析一行 CSV：支持双引号包裹字段与 "" 转义
static QStringList csvSplit(const QString& line)
{
    QStringList fields;
    QString cur;
    bool inQuote = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (inQuote) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
                else inQuote = false;
            } else {
                cur += ch;
            }
        } else if (ch == '"') {
            inQuote = true;
        } else if (ch == ',') {
            fields.append(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    fields.append(cur);
    return fields;
}

void DataImporter::do_dataImport(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit importFinished(false, QString("无法打开文件：%1").arg(file.errorString()));
        return;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    // 跳过标题行/日期行，找到表头（time_s,通道名,...）
    QString header;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty())  continue;
        if (line.startsWith("time_s", Qt::CaseInsensitive)) { header = line; break; }
    }
    if (header.isEmpty()) {
        emit importFinished(false, "CSV 格式错误：缺少 time_s 表头");
        return;
    }

    const int colCount = csvSplit(header).size() - 1;      // 去掉 time_s 列
    if (colCount < 1) {
        emit importFinished(false, "CSV 格式错误：没有数据列");
        return;
    }

    // 表头通道名回传，Manager 应用为图例名/导出列名
    QStringList names = csvSplit(header);
    names.removeFirst();                          // 去掉 time_s 列
    emit importNames(names);

    int lineCnt = 0, errorCnt = 0;
    // 按列收集到通道数据，最后一次性批量写入（单次加锁，避免逐点加锁开销）
    QList<ChannelData> dataList;
    dataList.resize(colCount);
    for (int i = 0; i < colCount; ++i)
        dataList[i].channel = i;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty())  continue;

        const QStringList fields = csvSplit(line);
        if (fields.size() < 2) { errorCnt++; continue; }

        bool timeOk = false;
        const double t = fields[0].trimmed().toDouble(&timeOk);
        if (!timeOk) { errorCnt++; continue; }

        // 按列导入：第 i+1 列对应通道 i，空单元格跳过
        const int cols = qMin(fields.size() - 1, colCount);
        for (int i = 0; i < cols; ++i) {
            const QString cell = fields[i + 1].trimmed();
            if (cell.isEmpty())  continue;
            bool valOk = false;
            const double v = cell.toDouble(&valOk);
            if (!valOk) { errorCnt++; continue; }
            dataList[i].times.append(t);
            dataList[i].values.append(v);
        }
        lineCnt++;
    }
    file.close();

    // 一次性批量写入（单次加锁；空通道由存储侧跳过）
    m_storage->addData(dataList);

    if (errorCnt > 0)
        emit importFinished(false, QString("导入完成，但存在 %1 处解析错误，共导入 %2 行").arg(errorCnt).arg(lineCnt));
    else
        emit importFinished(true, QString("成功导入 %1 行数据").arg(lineCnt));
}