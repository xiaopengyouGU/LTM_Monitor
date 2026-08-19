#ifndef CANFD_FRAME_MODEL_H
#define CANFD_FRAME_MODEL_H

#include <QAbstractTableModel>
#include <QMetaType>
#include "canfd_def.h"

#if defined(CANFD_WIDGET_LIBRARY)
#  define CANFD_WIDGET_EXPORT Q_DECL_EXPORT
#else
#  define CANFD_WIDGET_EXPORT Q_DECL_IMPORT
#endif

// 处理后的表格行：由中转站解析生成（格式化在源头做一次），模型直接存储，
// data() 不用实时格式化，显示零开销。
struct CANFD_WIDGET_EXPORT CanfdFrameRow
{
    QString sysTime;      // 系统时间（HH:mm:ss.zzz）
    QString devTime;      // 设备时间戳（0x... hex）
    QString channel;      // 通道（1/2）
    QString direction;    // Rx/Tx
    QString id;           // ID（0x...）
    QString frameFormat;  // 标准帧/扩展帧
    QString frameType;    // 数据帧/远程帧
    QString canfdType;    // CAN/CAN-FD/CAN-FD加速
    QString dlc;          // DLC 码
    QString data;         // 数据（x| xx xx）

    static CanfdFrameRow fromFrame(const CanfdFrame &frame, bool isTx);   // 帧 -> 显示行
};
Q_DECLARE_METATYPE(CanfdFrameRow)

// 帧表格模型：环形缓冲存储（预分配固定容量，写入 O(1)，超限自动覆盖最旧）。
// 缓冲/查表/序号状态全部收敛在 Private（Pimpl）中。
class CANFD_WIDGET_EXPORT CanfdFrameModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column
    {
        Col_Index = 0,      // 序号
        Col_SysTime,        // 系统时间（ms）
        Col_DevTime,        // 设备时间戳（hex）
        Col_Channel,        // 通道
        Col_Direction,      // 收发 Rx/Tx
        Col_Id,             // ID
        Col_FrameFormat,    // 标准帧/扩展帧
        Col_FrameType,      // 数据帧/远程帧
        Col_CANFD,          // CAN / CAN-FD / CAN-FD加速
        Col_Dlc,            // DLC
        Col_Data,           // 数据（hex）
        Col_Count
    };

    explicit CanfdFrameModel(int maxRows = 10000, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void appendRows(const QList<CanfdFrameRow> &rows);   // 批量追加（处理后的行）
    void clear();

private:
    Q_DISABLE_COPY(CanfdFrameModel)
    class Private;
    Private *pimpl = nullptr;
};

#endif // CANFD_FRAME_MODEL_H
