#include "app/Application.h"
#include "config/AppConfig.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>
#include <QTimer>

namespace {

struct CommandLine {
    QString configPath = QStringLiteral("config/default.toml");
    bool headless = false;
    bool simulateRobot = false;
};

CommandLine parseCommandLine(QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("C++ spray control minimal loop"));
    parser.addHelpOption();

    const QCommandLineOption configOption(
        QStringList{QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("Path to config file."),
        QStringLiteral("path"),
        QStringLiteral("config/default.toml"));
    const QCommandLineOption headlessOption(QStringLiteral("headless"), QStringLiteral("Run without HMI."));
    const QCommandLineOption simulateRobotOption(
        QStringLiteral("simulate-robot"),
        QStringLiteral("Use fake robot controller."));

    parser.addOption(configOption);
    parser.addOption(headlessOption);
    parser.addOption(simulateRobotOption);
    parser.process(app);

    return {
        parser.value(configOption),
        parser.isSet(headlessOption),
        parser.isSet(simulateRobotOption),
    };
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("spray_control"));

    const auto commandLine = parseCommandLine(app);
    QString error;
    const auto config = spray::config::AppConfig::load(commandLine.configPath, &error);
    if (!error.isEmpty()) {
        qWarning().noquote() << error << "- using defaults";
    }
    if (!config.validate(&error)) {
        qCritical().noquote() << "invalid config:" << error;
        return 2;
    }

    const bool simulateRobot = commandLine.simulateRobot || config.fakeRobot.enabled;

    QTextStream out(stdout);
    out << "spray_control starting\n";
    out << "mode=headless " << (commandLine.headless ? "true" : "false")
        << " simulate_robot " << (simulateRobot ? "true" : "false") << "\n";
    out << "config " << commandLine.configPath << "\n";
    out << "plc.enqueue " << config.plc.host << " " << config.plc.enqueuePort << "\n";
    out << "plc.dequeue " << config.plc.host << " " << config.plc.dequeuePort << "\n";
    out.flush();

    qInfo().noquote() << "spray_control starting";
    qInfo().noquote() << "mode=headless" << commandLine.headless
                      << "simulate_robot" << simulateRobot;
    qInfo().noquote() << "config" << commandLine.configPath;
    qInfo().noquote() << "plc.enqueue" << config.plc.host << config.plc.enqueuePort;
    qInfo().noquote() << "plc.dequeue" << config.plc.host << config.plc.dequeuePort;

    spray::app::Application application(config, simulateRobot);
    if (!application.initialize(&error)) {
        qCritical().noquote() << "initialize failed:" << error;
        return 3;
    }
    if (!application.start(&error)) {
        qCritical().noquote() << "start failed:" << error;
        return 4;
    }

    return app.exec();
}
