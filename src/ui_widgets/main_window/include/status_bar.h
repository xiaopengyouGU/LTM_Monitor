#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <QWidget>
#include <QLabel>
#include <QTimer>

class QBoxLayout;

class StatusBar : public QWidget
{
    Q_OBJECT
public:
    explicit StatusBar(QWidget *parent = nullptr);
    ~StatusBar();
    void setInfo(const QString &info);   //设置信息栏显示

private slots:
    void updateDateTime();   // 更新时间显示

private:
    void addSeparator(QBoxLayout *layout);   // 添加分隔线

    QLabel *labAuthor;
    QLabel *labWebside;
    QLabel *labInfo;
    QLabel *labTime;
    QTimer *m_timer;                        // 用于显示当前时间
};

#endif // STATUS_BAR_H

