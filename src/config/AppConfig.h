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

enum class RobotMode {
    Fake,
    Duco,
};

struct RobotConfig {
    RobotMode mode = RobotMode::Fake;
    QString ip = QStringLiteral("127.0.0.1");
    quint16 port = 7003;
    int heartbeatMs = 1000;
    bool prepareOnStart = false;
    bool autoPowerOn = true;
    bool autoEnable = true;
    QString recipePath = QStringLiteral("config/motion_recipes.toml");
};

enum class CameraFlowMode {
    LegacySingleCamera,
    DualCamera11_12,
};

enum class CameraCorrelationMode {
    Sequential,
    Counted,
};

enum class CameraCountExtractMode {
    Auto,
    Ascii,
    Binary,
    Disabled,
};

struct CameraConfig {
    QString host = QStringLiteral("0.0.0.0");
    quint16 camera2dPort = 9001;
    quint16 camera3dPort = 9002;
    bool camera3dEnabled = false;
    QString legacyHost = QStringLiteral("127.0.0.1");
    quint16 legacyPort = 9001;
    CameraFlowMode flowMode = CameraFlowMode::LegacySingleCamera;
    CameraCorrelationMode correlationMode = CameraCorrelationMode::Sequential;
    CameraCountExtractMode countExtractMode = CameraCountExtractMode::Auto;
};

struct LoggingConfig {
    bool rawFrames = true;
    QString logDir = QStringLiteral("logs");
    bool persistRawFrames = false;
    bool persistEvents = false;
    int maxInMemoryEvents = 1000;
    int flushIntervalMs = 1000;
};

struct ModbusConfig {
    bool enabled = false;
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 502;
    int defaultSlaveId = 1;
    int timeoutMs = 1000;
    int retries = 3;
    QString addressTablePath = QStringLiteral("config/modbus_nodes.toml");
};

struct AppConfig {
    PlcConfig plc;
    QueueConfig queue;
    FakeRobotConfig fakeRobot;
    RobotConfig robot;
    CameraConfig camera;
    LoggingConfig logging;
    ModbusConfig modbus;

    static AppConfig defaults();
    static AppConfig load(const QString& path, QString* errorMessage = nullptr);
    bool validate(QString* errorMessage = nullptr) const;
};

} // namespace spray::config
