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
        QVERIFY(config.validate(&error));
    }

    void rejectsInvalidRobotConfig()
    {
        auto config = AppConfig::defaults();
        config.robot.port = 0;

        QString error;
        QVERIFY(!config.validate(&error));
        QCOMPARE(error, QStringLiteral("robot config is invalid"));
    }
};

QObject* createAppConfigTest()
{
    return new AppConfigTest();
}

#include "test_app_config.moc"
