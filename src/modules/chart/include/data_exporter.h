#ifndef __DATA_EXPORTER_H__
#define __DATA_EXPORTER_H__

#include <QObject>

#if defined(CHART_LIBRARY)
#  define CHART_EXPORT Q_DECL_EXPORT
#else
#  define CHART_EXPORT Q_DECL_IMPORT
#endif

class DataStorage;          //前向声明 

class CHART_EXPORT DataExporter : public QObject{
    Q_OBJECT
public:
    explicit DataExporter(DataStorage *storage, QObject* parent = nullptr) : m_storage(storage),QObject(parent)
    {   Q_ASSERT_X(m_storage, "DataExporter", "storage cannot be nullptr");
        if(!m_storage) qDebug("DataImporter: storage is nullptr"); }

public slots:
    void do_dataExport(const QString& fileName, qint64 startTime, qint64 endTime);

signals:
    void exportFinished(bool success, const QString& message);
private:
    DataStorage *m_storage;
};

#endif