#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <QWidget>

#if defined(STATUS_BAR_LIBRARY)
#  define STATUS_BAR_EXPORT Q_DECL_EXPORT
#else
#  define STATUS_BAR_EXPORT Q_DECL_IMPORT
#endif

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
