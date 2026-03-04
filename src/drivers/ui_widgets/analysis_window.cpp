// #include "analysis_window.h"

// AnalysisWindow::AnalysisWindow(QObject *parent)
// {
//     m_chart = new QChart;
//     chart_manager = new ChartManager(m_chart);
// }

// AnalysisWindow::~AnalysisWindow()
// {
//     delete chart_manager;                   //先删除图表管理器        
//     delete m_chart;                         //再删除图表
// }

// void AnalysisWindow::loadCSV(const QString& fileName)
// {
//     //创建临时存储（与实时采集独立）
//     m_storage = new DataStorage(720000);    //1小时容量
//     m_importer = new DataImporter(m_storage);
//     QThread *thread = new QThread;
//     m_importer->moveToThread(thread);
    
//     connect(thread, &QThread::finished, m_importer, &QObject::deleteLater);
//     connect(thread, &QThread::finished, thread, &QObject::deleteLater);
//     connect(m_importer, &DataImporter::importFinished, this, &AnalysisWindow::do_importFinished);

//     thread->start();
//     emit startImport(fileName);
// }

// void AnalysisWindow::do_importFinished(bool success, const QString& msg)
// {
//     if(success)
//     {
//         QMessageBox::information(this, "导入成功", msg);
//         chart_manager->updataAll(m_storage);
//     }
//     else    
//         QMessageBox::critical(this, "导入失败", msg);
// }