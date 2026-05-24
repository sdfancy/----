#include "HmiCommandPort.h"
#include "app/Application.h"
#include <QMessageBox>
#include <QDebug>

namespace spray::ui {

HmiCommandPort::HmiCommandPort(std::shared_ptr<spray::app::Application> app, QObject* parent)
    : QObject(parent), m_app(std::move(app))
{
}

HmiCommandPort::~HmiCommandPort() = default;

void HmiCommandPort::requestStartSystem(QWidget* parentWindow)
{
    if (!m_app) return;

    // Check state, start if stopped
    qInfo() << "HMI requested system start";
    QString error;
    if (!m_app->start(&error)) {
        if (parentWindow) {
            QMessageBox::critical(parentWindow, "启动失败", "无法启动系统: " + error);
        }
    }
}

void HmiCommandPort::requestEmergencyStop(QWidget* parentWindow)
{
    if (!m_app) return;

    if (parentWindow) {
        auto reply = QMessageBox::warning(parentWindow, "紧急停止",
            "您确定要执行紧急停止吗？这将立即停止机器人并清空队列！",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    qInfo() << "HMI requested emergency stop";
    m_app->stop(); // Or specific emergency stop sequence
}

void HmiCommandPort::requestClearAlarms()
{
    qInfo() << "HMI requested clear alarms";
    // Delegate to diagnostics or robot controller
}

void HmiCommandPort::injectSimulatorPayload(int port, const QByteArray& data)
{
    qInfo() << "HMI requested simulator inject on port" << port << "data:" << data.toHex();
    // Pass to simulator
}

} // namespace spray::ui
