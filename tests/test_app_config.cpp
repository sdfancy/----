#include "test_app_config.h"

#include "config/AppConfig.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using spray::config::AppConfig;
using spray::config::RobotMode;

class AppConfigTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesRobotConfig()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.filePath(QStringLiteral("config.toml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(R"toml(
[robot]
mode = "duco"
ip = "192.168.1.66"
port = 7003
heartbeat_ms = 500
prepare_on_start = true
auto_power_on = false
auto_enable = false
recipe_path = "config/site_motion_recipes.toml"
)toml");
        file.close();

        QString error;
        const auto config = AppConfig::load(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(config.robot.mode, RobotMode::Duco);
        QCOMPARE(config.robot.ip, QStringLiteral("192.168.1.66"));
        QCOMPARE(config.robot.port, 7003);
        QCOMPARE(config.robot.heartbeatMs, 500);
        QVERIFY(config.robot.prepareOnStart);
        QVERIFY(!config.robot.autoPowerOn);
        QVERIFY(!config.robot.autoEnable);
        QCOMPARE(config.robot.recipePath, QStringLiteral("config/site_motion_recipes.toml"));
        QVERIFY(config.validate(&error));
    }

    void keepsDefaultRecipePath()
    {
        const auto config = AppConfig::defaults();
        QCOMPARE(config.robot.recipePath, QStringLiteral("config/motion_recipes.toml"));
    }

    void rejectsInvalidRobotConfig()
    {
        auto config = AppConfig::defaults();
        config.robot.port = 0;

        QString error;
        QVERIFY(!config.validate(&error));
        QCOMPARE(error, QStringLiteral("robot config is invalid"));
    }

    void parsesModbusConfig()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.filePath(QStringLiteral("config.toml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(R"toml(
[modbus]
enabled = true
host = "192.168.1.20"
port = 502
default_slave_id = 2
timeout_ms = 1500
retries = 2
address_table = "config/site_modbus.toml"
)toml");
        file.close();

        QString error;
        const auto config = AppConfig::load(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(config.modbus.enabled);
        QCOMPARE(config.modbus.host, QStringLiteral("192.168.1.20"));
        QCOMPARE(config.modbus.port, 502);
        QCOMPARE(config.modbus.defaultSlaveId, 2);
        QCOMPARE(config.modbus.timeoutMs, 1500);
        QCOMPARE(config.modbus.retries, 2);
        QCOMPARE(config.modbus.addressTablePath, QStringLiteral("config/site_modbus.toml"));
        QVERIFY(config.validate(&error));
    }

    void rejectsInvalidModbusConfig()
    {
        auto config = AppConfig::defaults();
        config.modbus.timeoutMs = 0;

        QString error;
        QVERIFY(!config.validate(&error));
        QCOMPARE(error, QStringLiteral("modbus config is invalid"));
    }

    void parsesLoggingConfig()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.filePath(QStringLiteral("config.toml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(R"toml(
[logging]
raw_frames = false
log_dir = "site_logs"
persist_raw_frames = true
persist_events = true
max_in_memory_events = 250
flush_interval_ms = 200
)toml");
        file.close();

        QString error;
        const auto config = AppConfig::load(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!config.logging.rawFrames);
        QCOMPARE(config.logging.logDir, QStringLiteral("site_logs"));
        QVERIFY(config.logging.persistRawFrames);
        QVERIFY(config.logging.persistEvents);
        QCOMPARE(config.logging.maxInMemoryEvents, 250);
        QCOMPARE(config.logging.flushIntervalMs, 200);
        QVERIFY(config.validate(&error));
    }

    void rejectsInvalidLoggingConfig()
    {
        auto config = AppConfig::defaults();
        config.logging.maxInMemoryEvents = 0;

        QString error;
        QVERIFY(!config.validate(&error));
        QCOMPARE(error, QStringLiteral("logging config is invalid"));
    }
};

QObject* createAppConfigTest()
{
    return new AppConfigTest();
}

#include "test_app_config.moc"
