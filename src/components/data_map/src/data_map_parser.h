#ifndef DATA_MAP_PARSER_H
#define DATA_MAP_PARSER_H

#include <QList>
#include <QString>
#include "data_map_def.h"

// DBC 文件解析（导入器）
bool parseDbcFile(const QString &filePath, QList<DataMapMessage> &messages, QString &error);

// 用户自定义协议解析（JSON 导入器）
bool parseJsonFile(const QString &filePath, QList<DataMapMessage> &messages, QString &error);

#endif // DATA_MAP_PARSER_H
