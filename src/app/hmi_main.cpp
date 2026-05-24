#include "app/Application.h"
#include "config/AppConfig.h"
#include "ui/ApplicationHmiRunner.h"

#include <QApplication>
#include <QDebug>
#include <QString>
#include <QTextStream>

#include <memory>

namespace {

struct HmiCommandLine {
    QString configPath = QStringLiteral("config/default.toml");
    bool simulateRobot = false;
};

HmiCommandLine parseHmiCommandLine(int argc, char* argv[])
{
    HmiCommandLine result;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if ((arg == QStringLiteral("--config") || arg == QStringLiteral("-c")) && i + 1 < argc) {
            result.configPath = QString::fromLocal8Bit(argv[++i]);
        } else if (arg.startsWith(QStringLiteral("--config="))) {
            result.configPath = arg.mid(QStringLiteral("--config=").size());
        } else if (arg == QStringLiteral("--simulate-robot")) {
            result.simulateRobot = true;
        }
    }
    return result;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("spray_hmi"));

    const auto commandLine = parseHmiCommandLine(argc, argv);

    QString error;
    const auto config = spray::config::AppConfig::load(commandLine.configPath, &error);
    if (!error.isEmpty()) {
        qWarning().noquote() << error << "- using defaults";
    }
    if (!config.validate(&error)) {
        qCritical().noquote() << "invalid config:" << error;
        return 2;
    }

    const bool simulateRobot = commandLine.simulateRobot || config.robot.mode == spray::config::RobotMode::Fake;

    QTextStream out(stdout);
    out << "spray_hmi starting\n";
    out << "simulate_robot " << (simulateRobot ? "true" : "false") << "\n";
    out << "config " << commandLine.configPath << "\n";
    out.flush();

    auto application = std::make_shared<spray::app::Application>(config, simulateRobot);
    if (!application->initialize(&error)) {
        qCritical().noquote() << "initialize failed:" << error;
        return 3;
    }
    if (!application->start(&error)) {
        qCritical().noquote() << "start failed:" << error;
        return 4;
    }

    return spray::ui::ApplicationHmiRunner::run(app, application, false);
}
