#pragma once

#include <QString>

namespace spray::config {

struct PlcConfig {
    QString host = QStringLiteral("0.0.0.0");
    quint16 enqueuePort = 9999;
    quint16 dequeuePort = 9090;
};

struct QueueConfig {
    int prefetchOffset = 0;
    int maxItemsPerArm = 128;
};

struct FakeRobotConfig {
    bool enabled = true;
    int acceptDelayMs = 20;
    int finishDelayMs = 80;
};

enum class CameraFlowMode {
    LegacySingleCamera,
    DualCamera11_12,
};

struct CameraConfig {
    QString host = QStringLiteral("0.0.0.0");
    quint16 camera2dPort = 9001;
    quint16 camera3dPort = 9002;
    bool camera3dEnabled = false;
    QString legacyHost = QStringLiteral("127.0.0.1");
    quint16 legacyPort = 9001;
    CameraFlowMode flowMode = CameraFlowMode::LegacySingleCamera;
};

struct LoggingConfig {
    bool rawFrames = true;
};

struct AppConfig {
    PlcConfig plc;
    QueueConfig queue;
    FakeRobotConfig fakeRobot;
    CameraConfig camera;
    LoggingConfig logging;

    static AppConfig defaults();
    static AppConfig load(const QString& path, QString* errorMessage = nullptr);
    bool validate(QString* errorMessage = nullptr) const;
};

} // namespace spray::config
