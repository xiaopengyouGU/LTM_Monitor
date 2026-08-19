#ifndef CHANNEL_LIST_WIDGET_H__
#define CHANNEL_LIST_WIDGET_H__

#include <QListWidget>

class QColor;       // 仅签名使用（const 引用），前向声明足够

// 通道列表控件：一行 = 曲线颜色 + 通道名 + 实际值 + 可见性
// 行由外部动态增删；颜色可点击修改，名字可编辑，可见性可勾选。
// 内部状态（通道号 -> 行映射）收敛在 Private（Pimpl）中。
class ChannelListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit ChannelListWidget(QWidget *parent = nullptr);
    ~ChannelListWidget();

    void addChannel(int channel, const QString& name, const QColor& color, bool visible);
    void removeChannel(int channel);
    void clearChannels();
    bool hasChannel(int channel) const;

    void setChannelName(int channel, const QString& name);      // 外部改名（如导入）时同步
    void setChannelColor(int channel, const QColor& color);
    void setChannelValue(int channel, double value, bool valid); // 实际值列（valid=false 显示 "-"）
    void setChannelVisible(int channel, bool visible);

signals:
    void nameEdited(int channel, const QString& name);
    void colorPicked(int channel, const QColor& color);
    void visibleToggled(int channel, bool visible);

private:
    Q_DISABLE_COPY(ChannelListWidget)
    class Private;
    Private *pimpl = nullptr;
};

#endif // CHANNEL_LIST_WIDGET_H__
