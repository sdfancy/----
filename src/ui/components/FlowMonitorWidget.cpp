#include "FlowMonitorWidget.h"
#include <QPainter>

namespace spray::ui {

FlowMonitorWidget::FlowMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
}

void FlowMonitorWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), QColor("#24283b"));

    painter.setPen(QColor("#a9b1d6"));
    painter.drawText(10, 20, "流程监控 (占位)");

    // Draw some mock nodes and lines
    painter.setPen(QPen(QColor("#414868"), 2));
    painter.drawLine(50, 100, 250, 100);

    painter.setBrush(QColor("#9ece6a"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(50, 100), 10, 10);
    painter.drawEllipse(QPoint(150, 100), 10, 10);
    painter.drawEllipse(QPoint(250, 100), 10, 10);
}

} // namespace spray::ui
