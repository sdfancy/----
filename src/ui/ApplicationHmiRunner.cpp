#include "ApplicationHmiRunner.h"
#include "MainWindow.h"
#include "SnapshotProvider.h"
#include "HmiCommandPort.h"
#include "app/Application.h"
#include <QApplication>
#include <QFile>
#include <QTimer>

namespace spray::ui {

int ApplicationHmiRunner::run(QCoreApplication& qtApp, std::shared_ptr<spray::app::Application> coreApp, bool headless)
{
    if (headless) {
        return qtApp.exec();
    }

    auto* guiApp = qobject_cast<QApplication*>(&qtApp);
    if (!guiApp) {
        qWarning("Not a QApplication, cannot start HMI");
        return qtApp.exec();
    }

    // Apply global stylesheet
    QFile qss(":/hmi/styles/dark_industrial.qss");
    if (qss.open(QFile::ReadOnly)) {
        guiApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
    }

    auto snapshotProvider = std::make_shared<SnapshotProvider>(coreApp);
    auto commandPort = std::make_shared<HmiCommandPort>(coreApp);

    MainWindow mainWindow(snapshotProvider, commandPort);
    mainWindow.show();

    snapshotProvider->start(500); // 500ms refresh rate

    return guiApp->exec();
}

} // namespace spray::ui
