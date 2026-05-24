#include "MainWindow.h"
#include "SnapshotProvider.h"
#include "HmiCommandPort.h"
#include "pages/OverviewPage.h"

#include <QToolBar>
#include <QStatusBar>
#include <QStackedWidget>
#include <QPushButton>

namespace spray::ui {

MainWindow::MainWindow(std::shared_ptr<SnapshotProvider> snapshotProvider,
                       std::shared_ptr<HmiCommandPort> commandPort,
                       QWidget* parent)
    : QMainWindow(parent),
      m_snapshotProvider(std::move(snapshotProvider)),
      m_commandPort(std::move(commandPort))
{
    setupUi();
    connect(m_snapshotProvider.get(), &SnapshotProvider::systemSnapshotUpdated,
            this, &MainWindow::onSystemSnapshotUpdated);
}

void MainWindow::setupUi()
{
    setWindowTitle("Spray Control HMI");
    resize(1024, 768);

    createToolBar();

    m_pageStack = new QStackedWidget(this);
    m_overviewPage = new OverviewPage(this);
    m_pageStack->addWidget(m_overviewPage);

    setCentralWidget(m_pageStack);

    statusBar()->showMessage("就绪");
}

void MainWindow::createToolBar()
{
    auto* toolBar = addToolBar("Main");
    toolBar->setMovable(false);

    auto* btnStart = new QPushButton(QIcon(":/hmi/icons/start.svg"), " 启动", this);
    btnStart->setObjectName("BtnStart");
    auto* btnStop = new QPushButton(QIcon(":/hmi/icons/stop.svg"), " 停止", this);
    btnStop->setObjectName("BtnStop");

    toolBar->addWidget(btnStart);
    toolBar->addWidget(btnStop);

    connect(btnStart, &QPushButton::clicked, this, [this]() {
        m_commandPort->requestStartSystem(this);
    });

    connect(btnStop, &QPushButton::clicked, this, [this]() {
        m_commandPort->requestEmergencyStop(this);
    });
}

void MainWindow::onSystemSnapshotUpdated(const spray::domain::SystemSnapshot& snapshot)
{
    // Pass to pages
    if (m_pageStack->currentWidget() == m_overviewPage) {
        m_overviewPage->applySnapshot(snapshot);
    }
}

} // namespace spray::ui
