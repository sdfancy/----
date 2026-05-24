#include "DeviceCardWidget.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace spray::ui {

DeviceCardWidget::DeviceCardWidget(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("CardBackground");

    m_lblTitle = new QLabel(title, this);
    m_lblTitle->setStyleSheet("font-weight: bold; font-size: 16px;");

    m_lblStatusIcon = new QLabel(this);
    m_lblStatusIcon->setFixedSize(16, 16);
    m_lblStatusText = new QLabel("Offline", this);

    m_lblRx = new QLabel("RX --:--:--", this);
    m_lblTx = new QLabel("TX --:--:--", this);

    auto statusLayout = new QHBoxLayout;
    statusLayout->addWidget(m_lblStatusIcon);
    statusLayout->addWidget(m_lblStatusText);
    statusLayout->addStretch();

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_lblTitle);
    mainLayout->addLayout(statusLayout);
    mainLayout->addWidget(m_lblRx);
    mainLayout->addWidget(m_lblTx);

    setStatus(false, "");
}

void DeviceCardWidget::setStatus(bool online, const QString& extraText)
{
    if (online) {
        m_lblStatusIcon->setStyleSheet("border-radius: 8px; background-color: #9ece6a;");
        m_lblStatusText->setText("Online " + extraText);
    } else {
        m_lblStatusIcon->setStyleSheet("border-radius: 8px; background-color: #f7768e;");
        m_lblStatusText->setText("Offline " + extraText);
    }
}

void DeviceCardWidget::updateTraffic(const QString& rxTime, const QString& txTime)
{
    m_lblRx->setText("RX " + rxTime);
    m_lblTx->setText("TX " + txTime);
}

} // namespace spray::ui
