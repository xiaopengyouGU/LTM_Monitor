#ifndef __DATA_IMPORTER_H__
#define __DATA_IMPORTER_H__

#include <QTextStream>
#include <QFile>
#include <QSet>
#include <limits>
#include "data_storage.h"
//CSV数据导入分析器

class DataImporter : public QObject{
    Q_OBJECT
public:
    explicit DataImporter(DataStorage *storage) : m_storage(storage)
    {   Q_ASSERT_X(m_storage, "DataImporter", "importer cannot be nullptr");}

public slots:
    void dataImport(const QString& fileName);
signals:
    void importFinished(bool success, const QString& msg);
    
private:
    DataStorage* m_storage;
};


#endif