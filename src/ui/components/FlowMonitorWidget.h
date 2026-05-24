#pragma once

#include <QWidget>

namespace spray::ui {

class FlowMonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit FlowMonitorWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace spray::ui
