#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <QWidget>

#if defined(STATUS_BAR_LIBRARY)
#  define STATUS_BAR_EXPORT Q_DECL_EXPORT
#else
#  define STATUS_BAR_EXPORT Q_DECL_IMPORT
#endif

// 产品版本号：状态栏显示 / 在线更新比对 / 发布脚本解析，三处同源，只改这一行
#define LTM_MONITOR_VERSION "0.4.5"
// 状态栏组件：信息显示 + 时间刷新。
// 供各 widget 通过 setStatusBar(StatusBar*) 直连注入
class STATUS_BAR_EXPORT StatusBar : public QWidget
{
    Q_OBJECT
public:
    explicit StatusBar(QWidget *parent = nullptr);
    ~StatusBar();
    void setInfo(const QString &info);       // 设置信息栏显示

private:
    Q_DISABLE_COPY(StatusBar)
    class Private;
    Private *pimpl;
};

#endif // STATUS_BAR_H
