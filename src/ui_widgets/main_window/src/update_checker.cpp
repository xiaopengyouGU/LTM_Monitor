#include "update_checker.h"
#include "status_bar.h"                 // LTM_MONITOR_VERSION：版本号唯一来源

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QXmlStreamReader>

namespace
{
    // 更新仓库索引：Gitee 仓库 installer 分支（与安装包 config.xml 的 RemoteRepositories 保持一致）
    const char *kRepoIndexUrl = "https://gitee.com/xiaopengyouGU/LTM_Monitor/raw/installer/Updates.xml";
    const char *kPackageName  = "LTM_Project";      // 主程序组件，其版本号即产品版本

    // 逐段按数值比较，避免字符串比较把 0.4.10 判成比 0.4.9 旧
    bool versionLessThan(const QString &lhs, const QString &rhs)
    {
        const QStringList a = lhs.split('.');
        const QStringList b = rhs.split('.');
        const int n = qMax(a.size(), b.size());
        for (int i = 0; i < n; ++i) {
            const int va = (i < a.size()) ? a.at(i).toInt() : 0;
            const int vb = (i < b.size()) ? b.at(i).toInt() : 0;
            if (va != vb)
                return va < vb;
        }
        return false;
    }
}

// ============================================================
// 私有实现（Pimpl）：网络与解析收敛于此
// ============================================================
class UpdateChecker::Private
{
public:
    explicit Private(UpdateChecker *owner) : owner(owner) {}

    void onReply(QNetworkReply *reply);

    UpdateChecker        *owner = nullptr;
    QNetworkAccessManager *net  = nullptr;
    bool                  busy  = false;      // 上一次请求未回来时忽略重复检查
};

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent), pimpl(new Private(this))
{
    pimpl->net = new QNetworkAccessManager(this);
    connect(pimpl->net, &QNetworkAccessManager::finished,
            this, [this](QNetworkReply *reply) { pimpl->onReply(reply); });
}

UpdateChecker::~UpdateChecker()
{
    delete pimpl;
}

QString UpdateChecker::currentVersion()
{
    return QStringLiteral(LTM_MONITOR_VERSION);
}

QString UpdateChecker::maintenanceToolPath()
{
    // 安装布局：<安装根>/LTM_Project/bin/LTM_Monitor.exe，maintenancetool.exe 在安装根下
    const QDir binDir(QCoreApplication::applicationDirPath());
    const QString tool = QDir::cleanPath(binDir.filePath("../..")) + "/maintenancetool.exe";
    return QFileInfo::exists(tool) ? tool : QString();
}

void UpdateChecker::check()
{
    if (pimpl->busy) return;
    pimpl->busy = true;

    QNetworkRequest request{ QUrl(kRepoIndexUrl) };
    request.setTransferTimeout(5000);                    // 5 秒拿不到索引就报失败，不拖着用户
    request.setRawHeader("User-Agent", "LTM-Monitor-Updater/1.0");
    pimpl->net->get(request);
}

void UpdateChecker::Private::onReply(QNetworkReply *reply)
{
    reply->deleteLater();
    busy = false;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || (status != 0 && status != 200)) {
        emit owner->finished(false, false, QString(), reply->errorString());
        return;
    }

    // 仓库索引里取 <Name>LTM_Project</Name> 那个包的 <Version>
    QXmlStreamReader xml(reply->readAll());
    QString pkgName, pkgVersion, latest;
    while (!xml.atEnd() && latest.isEmpty()) {
        if (!xml.readNextStartElement())
            continue;
        const QString tag = xml.name().toString();
        if (tag == QLatin1String("PackageUpdate")) {
            pkgName.clear();
            pkgVersion.clear();
            continue;
        }
        if (tag == QLatin1String("Name")) {
            pkgName = xml.readElementText().trimmed();
        } else if (tag == QLatin1String("Version")) {
            pkgVersion = xml.readElementText().trimmed();
            if (pkgName == QLatin1String(kPackageName))
                latest = pkgVersion;
        } else {
            xml.skipCurrentElement();
        }
    }

    if (latest.isEmpty()) {
        emit owner->finished(false, false, QString(), QString("更新仓库里没有找到主程序组件信息"));
        return;
    }

    const QString current = UpdateChecker::currentVersion();
    emit owner->finished(true, versionLessThan(current, latest), latest, QString());
}