#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <QObject>
#include <QString>

// 在线更新检查：拉取更新仓库索引（Updates.xml），与本地版本比对。
// 只回答“有没有新版本”，下载与安装交给安装器自带的 maintenancetool。
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker();

    void check();                                   // 发起检查（异步，结果走 finished 信号）

    static QString currentVersion();                // 本地版本（与状态栏显示、发布脚本同源）
    static QString maintenanceToolPath();           // 安装根目录下的 maintenancetool.exe；开发环境返回空

signals:
    // ok=false 表示网络/解析失败；hasUpdate=true 时 latest 是新版本号
    void finished(bool ok, bool hasUpdate, const QString &latest, const QString &message);

private:
    Q_DISABLE_COPY(UpdateChecker)
    class Private;
    Private *pimpl = nullptr;
};

#endif // UPDATE_CHECKER_H