#include "OverviewPage.h"
#include "components/DeviceCardWidget.h"
#include "components/FlowMonitorWidget.h"
#include "models/QueueTableModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTableView>
#include <QHeaderView>

namespace spray::ui {

OverviewPage::OverviewPage(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void OverviewPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Cards layout
    auto* cardsLayout = new QHBoxLayout;
    m_cardPlcIn = new DeviceCardWidget("PLC入队 9999", this);
    m_cardPlcOut = new DeviceCardWidget("PLC出队 9090", this);
    m_cardCam2D = new DeviceCardWidget("相机2D", this);
    m_cardArm1 = new DeviceCardWidget("机械臂1", this);
    m_cardArm2 = new DeviceCardWidget("机械臂2", this);

    cardsLayout->addWidget(m_cardPlcIn);
    cardsLayout->addWidget(m_cardPlcOut);
    cardsLayout->addWidget(m_cardCam2D);
    cardsLayout->addWidget(m_cardArm1);
    cardsLayout->addWidget(m_cardArm2);

    mainLayout->addLayout(cardsLayout);

    // Splitter for bottom area
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left: Table
    m_queueModel = new QueueTableModel(this);
    auto* tableView = new QTableView(this);
    tableView->setModel(m_queueModel);
    tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    splitter->addWidget(tableView);

    // Right: Flow Monitor
    m_flowMonitor = new FlowMonitorWidget(this);
    splitter->addWidget(m_flowMonitor);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    mainLayout->addWidget(splitter, 1);
}

void OverviewPage::applySnapshot(const spray::domain::SystemSnapshot& snapshot)
{
    // Update cards based on snapshot.devices / robots
    if (!snapshot.devices.isEmpty()) {
        const auto& plcIn = snapshot.devices.first(); // Simplified for demo
        m_cardPlcIn->setStatus(plcIn.online, "");
        m_cardPlcIn->updateTraffic(plcIn.lastRxAt.toString("HH:mm:ss"), plcIn.lastTxAt.toString("HH:mm:ss"));
    }

    if (!snapshot.robots.isEmpty()) {
        const auto& r1 = snapshot.robots.first();
        m_cardArm1->setStatus(r1.connected, r1.mode);
    }

    m_queueModel->updateData(snapshot.queue);
}

} // namespace spray::ui
