#pragma once

#include <QObject>
#include <memory>

class QWidget;

namespace spray::app {
class Application;
}

namespace spray::ui {

class HmiCommandPort : public QObject {
    Q_OBJECT
public:
    explicit HmiCommandPort(std::shared_ptr<spray::app::Application> app, QObject* parent = nullptr);
    ~HmiCommandPort() override;

    void requestStartSystem(QWidget* parentWindow);
    void requestEmergencyStop(QWidget* parentWindow);
    void requestClearAlarms();
    void injectSimulatorPayload(int port, const QByteArray& data);

private:
    std::shared_ptr<spray::app::Application> m_app;
};

} // namespace spray::ui
