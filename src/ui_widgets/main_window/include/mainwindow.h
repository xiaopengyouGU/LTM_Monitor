#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

// 主窗口壳：页面容器 + 菜单 + 状态栏 + 组件接线。
// 槽函数保留在外层（connectSlotsByName 自动连接需要）
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_btnSerial_clicked();                    // 打开 串口
    void on_btnCanfd_clicked();                     // 打开 CAN-FD
    void on_btnDataExport_clicked();                // CSV数据格式导出
    void on_btnChartShow_clicked();                 // 打开图表控制器

    void on_btnStart_clicked();                     // 下位机启停按钮
    void on_btnMode_clicked();                      // 显示模式切换
    void on_btnReset_clicked();                     // 发送复位命令
    void on_btnStopShow_clicked();                  // 停止显示所有曲线
    void on_btnClearShow_clicked();                 // 清空显示,一键清屏

    // 菜单栏对应的槽函数
    void on_actImportDB_triggered();                // 打开数据库
    void on_actOpenLog_triggered();                 // 打开日志分析器
    void on_actUseIntro_triggered();                // 使用说明
    void on_actShowCanfd_triggered(bool checked);   // 切换到 CAN-FD 窗口
    void on_actUDS_triggered(bool checked);         // 切换到 UDS 升级窗口
    void on_actTerminalUtf8_triggered();            // 控制台编码：UTF-8
    void on_actTerminalGbk_triggered();             // 控制台编码：GBK

private:
    class Private;
    Private *pimpl = nullptr;
};

#endif // MAINWINDOW_H
