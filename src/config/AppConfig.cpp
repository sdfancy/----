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
        } else if (section == QStringLiteral("logging")) {
            if (key == QStringLiteral("raw_frames")) {
                config.logging.rawFrames = parseBool(value, config.logging.rawFrames);
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
    return true;
}

} // namespace spray::config
