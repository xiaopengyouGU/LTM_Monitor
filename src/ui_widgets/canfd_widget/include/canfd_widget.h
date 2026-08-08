#ifndef CANFD_WIDGET_H
#define CANFD_WIDGET_H

#include <QWidget>
#include "canfd_def.h"
#include "canfd_frame_model.h"

#if defined(CANFD_WIDGET_LIBRARY)
#  define CANFD_WIDGET_EXPORT Q_DECL_EXPORT
#else
#  define CANFD_WIDGET_EXPORT Q_DECL_IMPORT
#endif

namespace Ui {
class CanfdWidget;
}
class CanfdManager;
class CanfdFrameModel;

// CAN-FD 调试界面：帧发送配置 + 帧接收表格
class CANFD_WIDGET_EXPORT CanfdWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CanfdWidget(QWidget *parent = nullptr);
    ~CanfdWidget();

    void connectManager(CanfdManager *manager);              // 绑定 CAN-FD 管理器

public slots:
    void onFrameReceived(const QList<CanfdFrameRow> &rows);  // 由中转站转发解析后的表格行
    void onFramesSent(const QList<CanfdFrameRow> &rows);     // 中转站转发的已发送表格行（Tx 回显）

private slots:
    void on_btnSend_clicked();
    void on_btnClear_clicked();
    void on_chkStopShow_toggled(bool checked);
    void on_chkStopSend_toggled(bool checked);

private:
    bool parseFrame(CanfdFrame &frame);       // 从控件解析一帧

    Ui::CanfdWidget *ui;
    CanfdManager    *m_manager   = nullptr;
    CanfdFrameModel *m_model     = nullptr;
    bool             m_followBottom = true;   // 表格是否自动跟随最新帧
    bool             m_paused = false;        // 暂停显示

};

#endif // CANFD_WIDGET_H
