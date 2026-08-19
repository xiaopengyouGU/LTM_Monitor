#include "canfd_widget.h"
#include "ui_canfd_widget.h"

#include "canfd.h"
#include "canfd_frame_model.h"

#include <QFontMetrics>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollBar>

// ============================================================
// 私有实现（Pimpl）：UI 与全部状态收敛于此
// ============================================================
class CanfdWidget::Private
{
public:
    explicit Private(CanfdWidget *q) : q(q) {}

    void setup();                       // UI 创建 + 表格模型配置
    void onSend();
    void onClear();
    void onStopShowToggled(bool checked);
    void onStopSendToggled(bool checked);
    void onRowsReceived(const QList<CanfdFrameRow> &rows);
    void onRowsSent(const QList<CanfdFrameRow> &rows);
    bool parseFrame(CanfdFrame &frame); // 从控件解析一帧

    CanfdWidget     *q = nullptr;
    Ui::CanfdWidget *ui = nullptr;
    CanfdManager    *m_manager   = nullptr;
    CanfdFrameModel *m_model     = nullptr;
    bool             m_followBottom = true;   // 表格是否自动跟随最新帧
    bool             m_paused = false;        // 暂停显示
};

void CanfdWidget::Private::setup()
{
    ui = new Ui::CanfdWidget;
    ui->setupUi(q);

    // 帧表格模型（有界 10000 行，超限自动删最旧）
    m_model = new CanfdFrameModel(10000, q);
    ui->tableView->setModel(m_model);
    ui->tableView->verticalHeader()->setVisible(false);
    // 数据列不省略、完整显示：横向滚动查看全部数据（64 字节最宽）
    ui->tableView->horizontalHeader()->setStretchLastSection(false);
    ui->tableView->setTextElideMode(Qt::ElideNone);
    ui->tableView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    QFontMetrics fm(ui->tableView->font());
    int dataW = fm.horizontalAdvance("64| 11 22 33 44 55 66 77 88 11 22 33 44 55 66 77 88 11 22 33 44 55 66 77 88 11 22 33 44 55 66 77 88 11 22 33 44 55 66 77 88 11 22 33 44 55 66 77 88 11 22 33 44 55 66 77 88 11 22 33 44 55 66 77 88") + 24;
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_Data, dataW);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_Index, 60);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_SysTime, 120);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_DevTime, 130);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_Channel, 40);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_Direction, 40);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_Id, 75);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_FrameFormat, 75);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_FrameType, 75);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_Dlc,   75);
    ui->tableView->setColumnWidth(CanfdFrameModel::Col_CANFD, 105);

    // 表格跟随：滚动条在底部时自动跟随最新帧，上滚查看历史时暂停
    connect(ui->tableView->verticalScrollBar(), &QScrollBar::valueChanged, q,
            [this](int value) {
                QScrollBar *bar = ui->tableView->verticalScrollBar();
                m_followBottom = (value >= bar->maximum() - 2);
            });
}

void CanfdWidget::Private::onSend()
{
    if (ui->chkStopSend->isChecked())
        return;                       // 暂停发送中
    CanfdFrame frame;
    if (!parseFrame(frame)) {         // 构造数据帧
        QMessageBox::warning(q, QString("发送"), QString("ID 或数据格式错误"));
        return;
    }
    // 发送任务下放 Worker：帧数 x 间隔由工作线程定时器控制，UI 只提交任务
    if (m_manager)
        m_manager->sendBatch(frame, ui->spinFrameCount->value(), ui->spinInterval->value(), false);
}

void CanfdWidget::Private::onRowsSent(const QList<CanfdFrameRow> &rows)
{
    if (m_paused)       return;         // 暂停显示：不回显
    m_model->appendRows(rows);          // 中转站解析后的已发送行直接入表
    if (m_followBottom)
        ui->tableView->scrollToBottom();
}

void CanfdWidget::Private::onRowsReceived(const QList<CanfdFrameRow> &rows)
{
    if (m_paused)       return;         // 暂停显示：丢弃
    m_model->appendRows(rows);          // 解析后的行直接入表
    if (m_followBottom)
        ui->tableView->scrollToBottom();
}

void CanfdWidget::Private::onClear()
{
    m_model->clear();
    m_followBottom = true;
}

void CanfdWidget::Private::onStopShowToggled(bool checked)
{
    m_paused = checked;
}

void CanfdWidget::Private::onStopSendToggled(bool checked)
{
    if (checked && m_manager)
        m_manager->stopSend();          // 停止 Worker 发送任务
}

bool CanfdWidget::Private::parseFrame(CanfdFrame &frame)
{
    bool ok = false;
    const uint32_t id = ui->editId->text().trimmed().toUInt(&ok, 16);
    const bool isExt = ui->comboFrameFormat->currentIndex() == 1;
    if (!ok || (isExt ? (id > 0x1FFFFFFFU) : (id > 0x7FFU)))
        return false;

    const int protocol = ui->comboProtocol->currentIndex();

    frame.id = CANFD_MAKE_ID(id, isExt, 0, 0);
    frame.channel = (uint8_t)ui->comboChannel->currentIndex();
    frame.isFd    =  protocol >= 1;                              // 1=CAN-FD，2=CAN-FD加速
    frame.flags   = (protocol == 2) ? 0x01U : 0U;                // CAN-FD加速，即BRS位使能
    frame.transmitType = (uint8_t)(ui->comboSendMode->currentIndex() == 1 ? 2 : 0);   // 2 = 自发自收

    // 解析 hex 数据（空格/逗号/分号分隔）
    QByteArray data;
    static QRegularExpression regular = QRegularExpression("[\\s,;]+");
    const QStringList tokens = ui->editData->text().split(regular, Qt::SkipEmptyParts);
    for (const QString &tok : tokens) {
        bool tokOk = false;
        const uint v = tok.toUInt(&tokOk, 16);
        if (!tokOk || v > 0xFFU)
            return false;
        data.append((char)v);
    }
    // 按 comboLen 目标长度处理：不足补 0，超出截断（同时受协议上限约束）
    int targetLen = ui->comboLen->currentText().toInt();
    const int maxLen = frame.isFd ? 64 : 8;
    if (targetLen > maxLen)
        targetLen = maxLen;
    if (data.size() < targetLen)
        data.append(targetLen - data.size(), '\0');
    else if (data.size() > targetLen)
        data.resize(targetLen);
    frame.len = (uint8_t)data.size();
    frame.data = data;

    return true;
}

// ============================================================
// 公共接口：委托给私有实现
// ============================================================
CanfdWidget::CanfdWidget(QWidget *parent)
    : QWidget(parent)
    , pimpl(new Private(this))
{
    pimpl->setup();
}

CanfdWidget::~CanfdWidget()
{
    delete pimpl;
}

void CanfdWidget::connectManager(CanfdManager *manager)
{
    if (manager) pimpl->m_manager = manager;
}

void CanfdWidget::onFrameReceived(const QList<CanfdFrameRow> &rows) { pimpl->onRowsReceived(rows); }
void CanfdWidget::onFramesSent(const QList<CanfdFrameRow> &rows)    { pimpl->onRowsSent(rows); }

void CanfdWidget::on_btnSend_clicked()      { pimpl->onSend(); }
void CanfdWidget::on_btnClear_clicked()     { pimpl->onClear(); }
void CanfdWidget::on_chkStopShow_toggled(bool checked) { pimpl->onStopShowToggled(checked); }
void CanfdWidget::on_chkStopSend_toggled(bool checked) { pimpl->onStopSendToggled(checked); }
