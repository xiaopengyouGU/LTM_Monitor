#include "data_map_parser.h"

#include <QFile>
#include <QRegularExpression>

// DBC 文件解析（v1 范围：BO_ / SG_ / VAL_，其余行忽略）
bool parseDbcFile(const QString &filePath, QList<DataMapMessage> &messages, QString &error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QString("无法打开 DBC 文件：%1").arg(filePath);
        return false;
    }
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    messages.clear();
    const QStringList lines = text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);

    static const QRegularExpression reBo("^BO_\\s+(\\d+)\\s+(\\S+)\\s*:\\s*(\\d+)\\s+(\\S+)\\s*$");
    static const QRegularExpression reSg("^SG_\\s+(\\S+)\\s*(M|m\\d+)?\\s*:\\s*(\\d+)\\|(\\d+)@([01])([+-])\\s*\\(([^,]+),\\s*([^)]+)\\)\\s*\\[([^|]*)\\|([^\\]]*)\\]\\s*\"([^\"]*)\"");
    static const QRegularExpression reVal("^VAL_\\s+(\\d+)\\s+(\\S+)\\s+(.+);\\s*$");
    static const QRegularExpression reValPair("(-?\\d+)\\s+\"([^\"]*)\"");

    DataMapMessage *cur = nullptr;
    for (const QString &line : lines) {
        QString t = line.trimmed();
        if (t.isEmpty())
            continue;

        QRegularExpressionMatch mBo = reBo.match(t);
        if (mBo.hasMatch()) {
            DataMapMessage msg;
            msg.id = mBo.captured(1).toUInt();
            msg.isExt = (msg.id > 0x7FF);           // 常规约定：超过 11 位范围的 ID 视为扩展帧
            msg.name = mBo.captured(2);
            msg.len = mBo.captured(3).toInt();
            messages.append(msg);
            cur = &messages.last();
            continue;
        }

        QRegularExpressionMatch mSg = reSg.match(t);
        if (mSg.hasMatch()) {
            if (!cur) {
                error = QString("SG_ 出现在 BO_ 之前：%1").arg(t);
                return false;
            }
            DataMapSignal sig;
            sig.name = mSg.captured(1);
            QString mux = mSg.captured(2);
            if (mux == "M"){ 
                sig.muxType = DataMap_MuxSelector;
            } else if (mux.startsWith("m")) {
                sig.muxType = DataMap_Muxed;
                sig.muxValue = mux.mid(1).toInt();
            }
            sig.startBit = mSg.captured(3).toInt();
            sig.length = mSg.captured(4).toInt();
            sig.byteOrder = (mSg.captured(5) == "1") ? DataMap_LittleEndian : DataMap_BigEndian;
            sig.valueType = (mSg.captured(6) == "+") ? DataMap_Unsigned : DataMap_Signed;
            sig.factor = mSg.captured(7).toDouble();
            sig.offset = mSg.captured(8).toDouble();
            sig.minVal = mSg.captured(9).toFloat();
            sig.maxVal = mSg.captured(10).toFloat();
            sig.unit = mSg.captured(11);
            cur->signalList.append(sig);
            continue;
        }

        QRegularExpressionMatch mVal = reVal.match(t);
        if (mVal.hasMatch()) {
            uint32_t id = mVal.captured(1).toUInt();
            QString sigName = mVal.captured(2);
            for (DataMapMessage &m : messages) {
                if (m.id != id)
                    continue;
                for (DataMapSignal &s : m.signalList) {
                    if (s.name == sigName) {
                        QRegularExpressionMatchIterator it = reValPair.globalMatch(mVal.captured(3));
                        while (it.hasNext()) {
                            QRegularExpressionMatch pm = it.next();
                            s.valueTable.insert(pm.captured(1).toInt(), pm.captured(2));
                        }
                        break;
                    }
                }
                break;
            }
            continue;
        }

        // CM_ / BA_ / BA_DEF_ / BU_ / NS_ 等行，v1 忽略
    }

    if (messages.isEmpty()) {
        error = QString("DBC 文件中未解析到任何消息：%1").arg(filePath);
        return false;
    }
    return true;
}
