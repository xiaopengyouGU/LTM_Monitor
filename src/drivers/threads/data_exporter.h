#ifndef __DATA_EXPORTER_H__
#define __DATA_EXPORTER_H__

#include <QObject>
#include <QFile>
#include <QSet>
#include <QMap>
#include "data_storage.h"

class DataExporter : public QObject{
    Q_OBJECT
public:
    explicit DataExporter(DataStorage *storage) : m_storage(storage)
    {  Q_ASSERT_X(m_storage, "DataExporter", "exporter cannot be nullptr"); }

public slots:
    void do_dataExport(const QString& fileName, qint64 startTime, qint64 endTime);

signals:
    void exportFinished(bool success, const QString& message);
private:
    DataStorage *m_storage;

};

#endif