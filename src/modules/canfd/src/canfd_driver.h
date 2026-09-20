#ifndef CANFD_DRIVER_H
#define CANFD_DRIVER_H

#include "ControlCANFD.h"
#include "canfd_def.h"

// 厂商 ControlCANFD.h 未声明该接口，但 DLL 已导出；两套驱动的内存布局一致。
typedef struct tagCanfdChannelErrInfo
{
    UINT error_code;
    BYTE passive_ErrData[3];
    BYTE arLost_ErrData;
} CanfdChannelErrInfo;

// CAN-FD 驱动加载器：QLibrary 与 ZCAN 函数表的唯一封装点。
// 驱动差异止步于此，CanfdController 及以上层不关心具体 DLL。
class CanfdDriver
{
public:
    // ZCAN 导出函数表：成员为 nullptr 表示当前 DLL 不支持该接口。
    struct Api
    {
        using OpenDeviceFn        = DEVICE_HANDLE (FUNC_CALL *)(UINT, UINT, UINT);
        using CloseDeviceFn       = UINT (FUNC_CALL *)(DEVICE_HANDLE);
        using GetDeviceInfoFn     = UINT (FUNC_CALL *)(DEVICE_HANDLE, ZCAN_DEVICE_INFO *);
        using IsDeviceOnLineFn    = UINT (FUNC_CALL *)(DEVICE_HANDLE);
        using InitCanFn           = CHANNEL_HANDLE (FUNC_CALL *)(DEVICE_HANDLE, UINT, ZCAN_CHANNEL_INIT_CONFIG *);
        using StartCanFn          = UINT (FUNC_CALL *)(CHANNEL_HANDLE);
        using GetReceiveNumFn     = UINT (FUNC_CALL *)(CHANNEL_HANDLE, BYTE);
        using TransmitFn          = UINT (FUNC_CALL *)(CHANNEL_HANDLE, ZCAN_Transmit_Data *, UINT);
        using ReceiveFn           = UINT (FUNC_CALL *)(CHANNEL_HANDLE, ZCAN_Receive_Data *, UINT, int);
        using TransmitFdFn        = UINT (FUNC_CALL *)(CHANNEL_HANDLE, ZCAN_TransmitFD_Data *, UINT);
        using ReceiveFdFn         = UINT (FUNC_CALL *)(CHANNEL_HANDLE, ZCAN_ReceiveFD_Data *, UINT, int);
        using GetIPropertyFn      = IProperty *(FUNC_CALL *)(DEVICE_HANDLE);
        using ReleaseIPropertyFn  = UINT (FUNC_CALL *)(IProperty *);
        using ReadChannelErrInfoFn = UINT (FUNC_CALL *)(CHANNEL_HANDLE, CanfdChannelErrInfo *);

        OpenDeviceFn         openDevice = nullptr;
        CloseDeviceFn        closeDevice = nullptr;
        GetDeviceInfoFn      getDeviceInf = nullptr;
        IsDeviceOnLineFn     isDeviceOnLine = nullptr;
        InitCanFn            initCan = nullptr;
        StartCanFn           startCan = nullptr;
        GetReceiveNumFn      getReceiveNum = nullptr;
        TransmitFn           transmit = nullptr;
        ReceiveFn            receive = nullptr;
        TransmitFdFn         transmitFd = nullptr;
        ReceiveFdFn          receiveFd = nullptr;
        GetIPropertyFn       getIProperty = nullptr;
        ReleaseIPropertyFn   releaseIProperty = nullptr;
        ReadChannelErrInfoFn readChannelErrInfo = nullptr;
    };

    CanfdDriver();
    ~CanfdDriver();

    QString load(const CanfdConfig &config);   // 返回空字符串表示加载成功
    void unload();
    bool isLoaded() const;
    const Api &api() const;
    QString loadedPath() const;

private:
    class Private;
    Private *pimpl = nullptr;
};

#endif // CANFD_DRIVER_H