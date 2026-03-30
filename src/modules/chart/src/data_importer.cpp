#include "data_importer.h"
#include "data_storage.h"
#include <QTextStream>
#include <QFile>
#include <QSet>
#include <limits>

void DataImporter::do_dataImport(const QString& fileName)
{
    QFile file(fileName);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        emit importFinished(false, QString("无法打开文件: %1").arg(file.errorString()));
        return;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    //跳过标题行
    if(in.atEnd())
    {
        emit importFinished(false, "文件为空");
        return;
    }
    in.readLine();

    int lineCnt = 0;
    int errorCnt = 0;
    
    while(!in.atEnd())
    {
        QString line = in.readLine();
        if(line.trimmed().isEmpty()) continue;      //跳过空行

        QStringList fields = line.split(',');       
        int count = (fields.size() - 1)/2;          //通道数
        if(count < 1 || count > 10)                 //判断一列对应的通道数
        {
            errorCnt++;
            continue;
        }

        bool ok;
        qint64 timestamp = fields[0].toLongLong(&ok);   //第一列为时间戳
        if(!ok)
        {
            errorCnt++;
            continue;
        }
        //逐通道解析
        for(int ch = 0; ch < count; ch++)
        {
            int targetIdx = 1 + ch * 2;                   //每通道占两列
            int actualIdx = targetIdx + 1;
            bool hasTarget = !fields[targetIdx].isEmpty();
            bool hasActual = !fields[actualIdx].isEmpty();
            //设计时，每个通道如果有实际值，那么一定有目标值，反之亦然。
            if(hasActual && hasTarget)
            {
                float targetVal = fields[targetIdx].toFloat();
                float actualVal = fields[actualIdx].toFloat();
                //虽然m_storage在另外一个线程中，但这个使用场景下，是为了做数据分析，所有不影响。
                m_storage->addData(ch, targetVal, actualVal, timestamp);
            }
        }
        lineCnt++;
    }
    file.close();

    if(errorCnt > 0)
        emit importFinished(false, QString("导入完成，但存在 %1 行解析错误，共导入 %2 行").arg(errorCnt).arg(lineCnt));
    else
        emit importFinished(true, QString("成功导入 %1 行数据").arg(lineCnt));

}