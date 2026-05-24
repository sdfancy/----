#include "config/AppConfig.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace spray::config {

namespace {

QString stripComment(QString line)
{
    const auto hashIndex = line.indexOf('#');
    if (hashIndex >= 0) {
        line.truncate(hashIndex);
    }
    return line.trimmed();
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.mid(1, value.size() - 2);
    }
    return value;
}

bool parseBool(const QString& value, bool fallback)
{
    const auto lowered = value.trimmed().toLower();
    if (lowered == QStringLiteral("true")) {
        return true;
    }
    if (lowered == QStringLiteral("false")) {
        return false;
    }
    return fallback;
}

int parseInt(const QString& value, int fallback)
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

quint16 parseU16(const QString& value, quint16 fallback)
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    if (!ok || parsed < 0 || parsed > 65535) {
        return fallback;
    }
    return static_cast<quint16>(parsed);
}

CameraFlowMode parseCameraFlowMode(const QString& value, CameraFlowMode fallback)
{
    const QString lowered = unquote(value).trimmed().toLower();
    if (lowered == QStringLiteral("legacy_single_camera")) {
        return CameraFlowMode::LegacySingleCamera;
    }
    if (lowered == QStringLiteral("dual_camera_11_12")) {
        return CameraFlowMode::DualCamera11_12;
    }
    return fallback;
}

RobotMode parseRobotMode(const QString& value, RobotMode fallback)
{
    const QString lowered = unquote(value).trimmed().toLower();
    if (lowered == QStringLiteral("fake")) {
        return RobotMode::Fake;
    }
    if (lowered == QStringLiteral("duco")) {
        return RobotMode::Duco;
    }
    return fallback;
}

CameraCorrelationMode parseCameraCorrelationMode(const QString& value, CameraCorrelationMode fallback)
{
    const QString lowered = unquote(value).trimmed().toLower();
    if (lowered == QStringLiteral("sequential")) {
        return CameraCorrelationMode::Sequential;
    }
    if (lowered == QStringLiteral("counted")) {
        return CameraCorrelationMode::Counted;
    }
    return fallback;
}

CameraCountExtractMode parseCameraCountExtractMode(const QString& value, CameraCountExtractMode fallback)
{
    const QString lowered = unquote(value).trimmed().toLower();
    if (lowered == QStringLiteral("auto")) {
        return CameraCountExtractMode::Auto;
    }
    if (lowered == QStringLiteral("ascii")) {
        return CameraCountExtractMode::Ascii;
    }
    if (lowered == QStringLiteral("binary")) {
        return CameraCountExtractMode::Binary;
    }
    if (lowered == QStringLiteral("disabled")) {
        return CameraCountExtractMode::Disabled;
    }
    return fallback;
}

} // namespace

AppConfig AppConfig::defaults()
{
    return {};
}

AppConfig AppConfig::load(const QString& path, QString* errorMessage)
{
    auto config = AppConfig::defaults();
    if (path.isEmpty()) {
        return config;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("cannot open config: %1").arg(path);
        }
        return config;
    }

    QString section;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto line = stripComment(stream.readLine());
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }

        const auto equalIndex = line.indexOf('=');
        if (equalIndex < 0) {
            continue;
        }

        const auto key = line.left(equalIndex).trimmed();
        const auto value = line.mid(equalIndex + 1).trimmed();

        if (section == QStringLiteral("plc")) {
            if (key == QStringLiteral("host")) {
                config.plc.host = unquote(value);
            } else if (key == QStringLiteral("enqueue_port")) {
                config.plc.enqueuePort = static_cast<quint16>(parseInt(value, config.plc.enqueuePort));
            } else if (key == QStringLiteral("dequeue_port")) {
                config.plc.dequeuePort = static_cast<quint16>(parseInt(value, config.plc.dequeuePort));
            }
        } else if (section == QStringLiteral("queue")) {
            if (key == QStringLiteral("prefetch_offset")) {
                config.queue.prefetchOffset = parseInt(value, config.queue.prefetchOffset);
            } else if (key == QStringLiteral("max_items_per_arm")) {
                config.queue.maxItemsPerArm = parseInt(value, config.queue.maxItemsPerArm);
            }
        } else if (section == QStringLiteral("fake_robot")) {
            if (key == QStringLiteral("enabled")) {
                config.fakeRobot.enabled = parseBool(value, config.fakeRobot.enabled);
            } else if (key == QStringLiteral("accept_delay_ms")) {
                config.fakeRobot.acceptDelayMs = parseInt(value, config.fakeRobot.acceptDelayMs);
            } else if (key == QStringLiteral("finish_delay_ms")) {
                config.fakeRobot.finishDelayMs = parseInt(value, config.fakeRobot.finishDelayMs);
            }
        } else if (section == QStringLiteral("robot")) {
            if (key == QStringLiteral("mode")) {
                config.robot.mode = parseRobotMode(value, config.robot.mode);
            } else if (key == QStringLiteral("ip")) {
                config.robot.ip = unquote(value);
            } else if (key == QStringLiteral("port")) {
                config.robot.port = static_cast<quint16>(parseInt(value, config.robot.port));
            } else if (key == QStringLiteral("heartbeat_ms")) {
                config.robot.heartbeatMs = parseInt(value, config.robot.heartbeatMs);
            } else if (key == QStringLiteral("prepare_on_start")) {
                config.robot.prepareOnStart = parseBool(value, config.robot.prepareOnStart);
            } else if (key == QStringLiteral("auto_power_on")) {
                config.robot.autoPowerOn = parseBool(value, config.robot.autoPowerOn);
            } else if (key == QStringLiteral("auto_enable")) {
                config.robot.autoEnable = parseBool(value, config.robot.autoEnable);
            }
        } else if (section == QStringLiteral("camera")) {
            if (key == QStringLiteral("host")) {
                config.camera.host = unquote(value);
            } else if (key == QStringLiteral("camera2d_port")) {
                config.camera.camera2dPort = static_cast<quint16>(parseInt(value, config.camera.camera2dPort));
            } else if (key == QStringLiteral("camera3d_port")) {
                config.camera.camera3dPort = static_cast<quint16>(parseInt(value, config.camera.camera3dPort));
            } else if (key == QStringLiteral("camera3d_enabled")) {
                config.camera.camera3dEnabled = parseBool(value, config.camera.camera3dEnabled);
            } else if (key == QStringLiteral("legacy_host")) {
                config.camera.legacyHost = unquote(value);
            } else if (key == QStringLiteral("legacy_port")) {
                config.camera.legacyPort = static_cast<quint16>(parseInt(value, config.camera.legacyPort));
            } else if (key == QStringLiteral("flow_mode")) {
                config.camera.flowMode = parseCameraFlowMode(value, config.camera.flowMode);
            } else if (key == QStringLiteral("camera_correlation_mode")) {
                config.camera.correlationMode = parseCameraCorrelationMode(value, config.camera.correlationMode);
            } else if (key == QStringLiteral("camera_count_extract_mode")) {
                config.camera.countExtractMode = parseCameraCountExtractMode(value, config.camera.countExtractMode);
            }
        } else if (section == QStringLiteral("logging")) {
            if (key == QStringLiteral("raw_frames")) {
                config.logging.rawFrames = parseBool(value, config.logging.rawFrames);
            }
        } else if (section == QStringLiteral("modbus")) {
            if (key == QStringLiteral("enabled")) {
                config.modbus.enabled = parseBool(value, config.modbus.enabled);
            } else if (key == QStringLiteral("host")) {
                config.modbus.host = unquote(value);
            } else if (key == QStringLiteral("port")) {
                config.modbus.port = parseU16(value, config.modbus.port);
            } else if (key == QStringLiteral("default_slave_id")) {
                config.modbus.defaultSlaveId = parseInt(value, config.modbus.defaultSlaveId);
            } else if (key == QStringLiteral("timeout_ms")) {
                config.modbus.timeoutMs = parseInt(value, config.modbus.timeoutMs);
            } else if (key == QStringLiteral("retries")) {
                config.modbus.retries = parseInt(value, config.modbus.retries);
            } else if (key == QStringLiteral("address_table")) {
                config.modbus.addressTablePath = unquote(value);
            }
        }
    }

    return config;
}

bool AppConfig::validate(QString* errorMessage) const
{
    if (plc.enqueuePort == 0 || plc.dequeuePort == 0 || plc.enqueuePort == plc.dequeuePort) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("PLC ports must be non-zero and different");
        }
        return false;
    }
    if (queue.prefetchOffset < 0 || queue.maxItemsPerArm <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("queue config is invalid");
        }
        return false;
    }
    if (fakeRobot.acceptDelayMs < 0 || fakeRobot.finishDelayMs < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("fake robot delays must be >= 0");
        }
        return false;
    }
    if (robot.port == 0 || robot.heartbeatMs <= 0 || robot.ip.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("robot config is invalid");
        }
        return false;
    }
    if (camera.camera2dPort == 0 || camera.legacyPort == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("camera ports must be non-zero");
        }
        return false;
    }
    if (camera.camera3dEnabled
        && (camera.camera3dPort == 0 || camera.camera3dPort == camera.camera2dPort)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("camera 2D/3D ports must be non-zero and different");
        }
        return false;
    }
    if (modbus.port == 0
        || modbus.defaultSlaveId <= 0
        || modbus.defaultSlaveId > 247
        || modbus.timeoutMs <= 0
        || modbus.retries < 0
        || modbus.host.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("modbus config is invalid");
        }
        return false;
    }
    return true;
}

} // namespace spray::config
