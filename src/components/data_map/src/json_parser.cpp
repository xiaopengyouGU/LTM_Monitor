#include "data_map_parser.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// 用户自定义协议解析（JSON，字段属性与 DBC 同构）
bool parseJsonFile(const QString &filePath, QList<DataMapMessage> &messages, QString &error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QString("无法打开协议文件：%1").arg(filePath);
        return false;
    }

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        error = QString("JSON 解析失败：%1（偏移 %2）").arg(perr.errorString()).arg(perr.offset);
        return false;
    }

    messages.clear();
    QJsonArray arr = doc.object().value("messages").toArray();
    if (arr.isEmpty()) {
        error = QString("未找到 messages 数组：%1").arg(filePath);
        return false;
    }

    for (const QJsonValue &v : arr) {
        QJsonObject obj = v.toObject();
        DataMapMessage msg;
        msg.id    = obj.value("id").toVariant().toUInt();
        msg.isExt = obj.value("extended").toBool(false);
        msg.name  = obj.value("name").toString();
        msg.len   = obj.value("dlc").toInt(8);

        QJsonArray sigs = obj.value("signals").toArray();
        if (sigs.isEmpty()) {
            error = QString("消息 %1 无信号定义").arg(msg.name);
            return false;
        }
        for (const QJsonValue &sv : sigs) {
            QJsonObject so = sv.toObject();
            DataMapSignal sig;
            sig.name = so.value("name").toString();
            sig.startBit = so.value("start_bit").toInt(-1);
            sig.length = so.value("length").toInt(-1);
            if (sig.name.isEmpty() || sig.startBit < 0 || sig.length < 1) {
                error = QString("信号定义不完整：%1").arg(sig.name);
                return false;
            }
            sig.byteOrder = (so.value("byte_order").toString("little") == "big") ? DataMap_BigEndian : DataMap_LittleEndian;
            sig.valueType = (so.value("value_type").toString("unsigned") == "signed") ? DataMap_Signed : DataMap_Unsigned;
            sig.factor = so.value("factor").toDouble(1.0);
            sig.offset = so.value("offset").toDouble(0.0);
            sig.unit = so.value("unit").toString();
            sig.minVal = (float)so.value("min").toDouble(0.0);
            sig.maxVal = (float)so.value("max").toDouble(0.0);

            QString mux = so.value("mux").toString("none");
            if (mux == "selector"){
                sig.muxType = DataMap_MuxSelector;
            } else if (mux == "multiplexed") {
                sig.muxType = DataMap_Muxed;
                sig.muxValue = so.value("mux_value").toInt(0);
            }

            QJsonObject vt = so.value("value_table").toObject();
            for (auto it = vt.constBegin(); it != vt.constEnd(); ++it)
                sig.valueTable.insert(it.key().toInt(), it.value().toString());

            sig.chartChannel = so.value("channel").toInt(0);

            msg.signalList.append(sig);
        }
        messages.append(msg);
    }
    return true;
}
