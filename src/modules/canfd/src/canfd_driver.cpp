#include "canfd_driver.h"

#include <QCoreApplication>
#include <QLibrary>
#include <QStringList>

namespace
{
    // 按顺序去重追加候选路径，保留首选驱动的优先级。
    void appendUnique(QStringList &paths, const QString &path)
    {
        if (!path.isEmpty() && !paths.contains(path))
            paths.append(path);
    }

    // 厂商只决定首选 DLL；两套驱动的 ZCAN 函数表一致。
    QString driverNameFor(int vendor)
    {
        if (vendor == CANFD_VENDOR_ZQWL)
            return QStringLiteral("zlgcan.dll");
        return QStringLiteral("ControlCANFD.dll");
    }

    QString fallbackDriverName(const QString &driverName)
    {
        if (driverName.compare(QStringLiteral("zlgcan.dll"), Qt::CaseInsensitive) == 0)
            return QStringLiteral("ControlCANFD.dll");
        return QStringLiteral("zlgcan.dll");
    }
}

class CanfdDriver::Private
{
public:
    QLibrary library;
    Api      api;
    QString  loadedPath;
    bool     loaded = false;

    template <typename T>
    bool resolve(const char *name, T &func)
    {
        func = reinterpret_cast<T>(library.resolve(name));
        return func != nullptr;
    }

    bool resolveRequired(QStringList &missing)
    {
        missing.clear();

        if (!resolve("ZCAN_OpenDevice", api.openDevice))
            missing.append(QStringLiteral("ZCAN_OpenDevice"));
        if (!resolve("ZCAN_CloseDevice", api.closeDevice))
            missing.append(QStringLiteral("ZCAN_CloseDevice"));
        if (!resolve("ZCAN_GetDeviceInf", api.getDeviceInf))
            missing.append(QStringLiteral("ZCAN_GetDeviceInf"));
        if (!resolve("ZCAN_IsDeviceOnLine", api.isDeviceOnLine))
            missing.append(QStringLiteral("ZCAN_IsDeviceOnLine"));
        if (!resolve("ZCAN_InitCAN", api.initCan))
            missing.append(QStringLiteral("ZCAN_InitCAN"));
        if (!resolve("ZCAN_StartCAN", api.startCan))
            missing.append(QStringLiteral("ZCAN_StartCAN"));
        if (!resolve("ZCAN_GetReceiveNum", api.getReceiveNum))
            missing.append(QStringLiteral("ZCAN_GetReceiveNum"));
        if (!resolve("ZCAN_Transmit", api.transmit))
            missing.append(QStringLiteral("ZCAN_Transmit"));
        if (!resolve("ZCAN_Receive", api.receive))
            missing.append(QStringLiteral("ZCAN_Receive"));
        if (!resolve("ZCAN_TransmitFD", api.transmitFd))
            missing.append(QStringLiteral("ZCAN_TransmitFD"));
        if (!resolve("ZCAN_ReceiveFD", api.receiveFd))
            missing.append(QStringLiteral("ZCAN_ReceiveFD"));
        if (!resolve("GetIProperty", api.getIProperty))
            missing.append(QStringLiteral("GetIProperty"));
        if (!resolve("ReleaseIProperty", api.releaseIProperty))
            missing.append(QStringLiteral("ReleaseIProperty"));

        return missing.isEmpty();
    }

    QStringList candidatePaths(const CanfdConfig &config) const
    {
        QStringList paths;

        if (!config.driverPath.isEmpty())
            appendUnique(paths, config.driverPath);

        const QString primary  = driverNameFor(config.vendor);
        const QString fallback = fallbackDriverName(primary);
        const QString appDir   = QCoreApplication::applicationDirPath();

        appendUnique(paths, appDir + "/" + primary);
        appendUnique(paths, appDir + "/drivers/" + primary);
        appendUnique(paths, appDir + "/" + fallback);
        appendUnique(paths, appDir + "/drivers/" + fallback);
        appendUnique(paths, primary);
        appendUnique(paths, fallback);
        return paths;
    }
};

CanfdDriver::CanfdDriver()
    : pimpl(new Private) {}

CanfdDriver::~CanfdDriver()
{
    unload();
    delete pimpl;
}

QString CanfdDriver::load(const CanfdConfig &config)
{
    unload();

    const QStringList candidates = pimpl->candidatePaths(config);
    QStringList errors;

    for (const QString &path : candidates) {
        pimpl->library.setFileName(path);
        if (!pimpl->library.load()) {
            errors.append(QString("%1：%2").arg(path, pimpl->library.errorString()));
            pimpl->library.unload();
            continue;
        }

        pimpl->api = Api();
        QStringList missing;
        if (!pimpl->resolveRequired(missing)) {
            errors.append(QString("%1：缺少导出函数 %2").arg(path, missing.join("、")));
            pimpl->library.unload();
            continue;
        }

        // 旧 DLL 也可能导出该接口，拿不到时仅关闭总线错误读取。
        pimpl->resolve("ZCAN_ReadChannelErrInfo", pimpl->api.readChannelErrInfo);

        pimpl->loadedPath = pimpl->library.fileName();
        pimpl->loaded = true;
        return QString();
    }

    pimpl->library.setFileName(QString());
    return QString("CAN-FD 驱动加载失败：\n%1").arg(errors.join("\n"));
}

void CanfdDriver::unload()
{
    if (pimpl->library.isLoaded())
        pimpl->library.unload();

    pimpl->api = Api();
    pimpl->loadedPath.clear();
    pimpl->loaded = false;
}

bool CanfdDriver::isLoaded() const
{
    return pimpl->loaded;
}

const CanfdDriver::Api &CanfdDriver::api() const
{
    return pimpl->api;
}

QString CanfdDriver::loadedPath() const
{
    return pimpl->loadedPath;
}