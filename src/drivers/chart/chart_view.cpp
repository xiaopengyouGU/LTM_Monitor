#include "chart_view.h"

ChartView::ChartView(QWidget *parent) : QChartView(parent) 
{
    isPanning = false;
    lastPanPos = QPointF();
}

void ChartView::wheelEvent(QWheelEvent *event)
{   //只在手动模式下启用缩放
    if (!chart() || !m_manager || m_manager->getMode() != 1) 
    {
        QChartView::wheelEvent(event);
        return;
    }

    // 获取坐标轴
    QValueAxis *axisX = nullptr;
    QValueAxis *axisY = nullptr;
    const auto axesX = chart()->axes(Qt::Horizontal);
    const auto axesY = chart()->axes(Qt::Vertical);
    if (!axesX.isEmpty()) axisX = qobject_cast<QValueAxis*>(axesX.first());
    if (!axesY.isEmpty()) axisY = qobject_cast<QValueAxis*>(axesY.first());
    if (!axisX || !axisY) return;

    // 获取绘图区域和鼠标位置
    QRectF plotArea = chart()->plotArea();
    QPointF pos = event->position(); 

    // 计算鼠标到绘图区四条边的距离（像素）
    double distLeft   = qAbs(pos.x() - plotArea.left());
    double distRight  = qAbs(pos.x() - plotArea.right());
    double distTop    = qAbs(pos.y() - plotArea.top());
    double distBottom = qAbs(pos.y() - plotArea.bottom());

    const int THRESHOLD = 30; // 可根据实际调整

    // 判断靠近哪条轴：Y轴（左边），X轴（底边）
    bool nearY = (distLeft < THRESHOLD);
    bool nearX = (distBottom < THRESHOLD);

    // 缩放因子, angleDelta返回鼠标滚轮的滚动角度
    // 向前滚动：放大曲线； 向后滚动：缩小曲线
    qreal factor = (event->angleDelta().y() > 0) ? 0.8 : 1.2;
    // 缩放中心（图表坐标）
    QPointF center = chart()->mapToValue(pos);

    // 根据区域决定缩放轴
    if (nearX && nearY) {
        // 靠近X轴和Y轴交点：同时缩放
        zoomAxis(axisX, center.x(), factor);
        zoomAxis(axisY, center.y(), factor);
    } else if (nearX) {
        // 靠近X轴：只缩放X
        zoomAxis(axisX, center.x(), factor);
    } else if (nearY) {
        // 靠近Y轴：只缩放Y
        zoomAxis(axisY, center.y(), factor);
    } else {
        // 远离坐标轴：同时缩放XY（也可选择不做任何事）
        zoomAxis(axisX, center.x(), factor);
        zoomAxis(axisY, center.y(), factor);
    }

    event->accept();
}

void ChartView::mousePressEvent(QMouseEvent *event)
{
    if(m_manager && m_manager->getMode() == 1 && event->button() == Qt::LeftButton) 
    {
        isPanning = true;
        lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QChartView::mousePressEvent(event);
} 

void ChartView::mouseMoveEvent(QMouseEvent *event)              //鼠标移动，平移更新
{
    if(isPanning) 
    {
        QPointF delta = event->pos() - lastPanPos;
        if (!delta.isNull()) 
        {
            // 获取绘图区域尺寸
            QRectF plotArea = chart()->plotArea();
            QValueAxis *axisX = nullptr;
            QValueAxis *axisY = nullptr;
            const auto axesX = chart()->axes(Qt::Horizontal);
            const auto axesY = chart()->axes(Qt::Vertical);
            axisX = qobject_cast<QValueAxis*>(axesX.first());
            axisY = qobject_cast<QValueAxis*>(axesY.first());

            if(axisX && axisY && plotArea.width() > 0 && plotArea.height() > 0) 
            {
                double xRange = axisX->max() - axisX->min();
                double yRange = axisY->max() - axisY->min();
                  // 添加阻尼因子，减小平移速度
                const double PAN_SPEED_FACTOR = 0.3; // 可根据需要调整（0.1~0.5）
                double dx = delta.x() * xRange / plotArea.width() * PAN_SPEED_FACTOR;
                double dy = -delta.y() * yRange / plotArea.height() * PAN_SPEED_FACTOR;

                axisX->setRange(axisX->min() - dx, axisX->max() - dx);
                axisY->setRange(axisY->min() - dy, axisY->max() - dy);
            }
        }
    }
    QChartView::mouseMoveEvent(event);
}

void ChartView::mouseReleaseEvent(QMouseEvent *event)            //鼠标释放，停止平移
{
    if(isPanning && event->button() == Qt::LeftButton) 
    {
        isPanning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QChartView::mouseReleaseEvent(event);
}

void ChartView::setChartManager(ChartManager *manager)
{
    m_manager = manager;
}

void ChartView::zoomAxis(QValueAxis *axis, double center, qreal factor)
{
    double min = axis->min();
    double max = axis->max();
    double newMin = center - (center - min) * factor;
    double newMax = center + (max - center) * factor;
    axis->setRange(newMin, newMax);
}