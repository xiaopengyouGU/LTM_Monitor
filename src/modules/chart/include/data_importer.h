#ifndef __DATA_IMPORTER_H__
#define __DATA_IMPORTER_H__

//CSV数据导入
#include <QObject>

#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif

class DataStorage;              //前向声明

class CHART_EXPORT DataImporter : public QObject{
    Q_OBJECT
public:
    explicit DataImporter(DataStorage *storage, QObject* parent = nullptr) : m_storage(storage),QObject(parent)
    {   Q_ASSERT_X(m_storage, "DataImporter", "storage cannot be nullptr");
        if(!m_storage) qDebug("DataImporter: storage is nullptr");
    }

public slots:
    void do_dataImport(const QString& fileName);
signals:
    void importFinished(bool success, const QString& msg);
    
private:
    DataStorage* m_storage;
};


#endif