#pragma once

#include <QWidget>
#include <vector>

namespace spray::domain {
struct SystemSnapshot;
}

namespace spray::ui {

class DeviceCardWidget;
class QueueTableModel;
class FlowMonitorWidget;
class EventLogTableModel;

class OverviewPage : public QWidget {
    Q_OBJECT
public:
    explicit OverviewPage(QWidget* parent = nullptr);

    void applySnapshot(const spray::domain::SystemSnapshot& snapshot);

private:
    void setupUi();

    DeviceCardWidget* m_cardPlcIn;
    DeviceCardWidget* m_cardPlcOut;
    DeviceCardWidget* m_cardCam2D;
    DeviceCardWidget* m_cardArm1;
    DeviceCardWidget* m_cardArm2;

    QueueTableModel* m_queueModel;
    FlowMonitorWidget* m_flowMonitor;
    EventLogTableModel* m_logModel;
};

} // namespace spray::ui
