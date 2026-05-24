#pragma once

#include <QWidget>

class QLabel;

namespace spray::ui {

class DeviceCardWidget : public QWidget {
    Q_OBJECT
public:
    explicit DeviceCardWidget(const QString& title, QWidget* parent = nullptr);

    void setStatus(bool online, const QString& extraText);
    void updateTraffic(const QString& rxTime, const QString& txTime);

private:
    QLabel* m_lblTitle;
    QLabel* m_lblStatusIcon;
    QLabel* m_lblStatusText;
    QLabel* m_lblRx;
    QLabel* m_lblTx;
};

} // namespace spray::ui
