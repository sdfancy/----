#include "MainWindow.h"
#include "SnapshotProvider.h"
#include "HmiCommandPort.h"
#include "pages/OverviewPage.h"

#include <QToolBar>
#include <QStatusBar>
#include <QStackedWidget>
#include <QPushButton>
#include <QListWidget>
#include <QHBoxLayout>

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

    m_sidebar = new QListWidget(this);
    m_sidebar->setObjectName("Sidebar");
    m_sidebar->setFixedWidth(150);
    
    auto* itemOverview = new QListWidgetItem(QIcon(":/hmi/icons/menu_overview.svg"), "总览面板");
    auto* itemQueue = new QListWidgetItem(QIcon(":/hmi/icons/menu_queue.svg"), "主队列");
    auto* itemDevice = new QListWidgetItem(QIcon(":/hmi/icons/menu_device.svg"), "设备状态");
    auto* itemLog = new QListWidgetItem(QIcon(":/hmi/icons/menu_log.svg"), "报警日志");
    auto* itemConfig = new QListWidgetItem(QIcon(":/hmi/icons/menu_config.svg"), "系统配置");

    m_sidebar->addItem(itemOverview);
    m_sidebar->addItem(itemQueue);
    m_sidebar->addItem(itemDevice);
    m_sidebar->addItem(itemLog);
    m_sidebar->addItem(itemConfig);
    
    m_sidebar->setCurrentRow(0);

    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_sidebar);
    mainLayout->addWidget(m_pageStack, 1);

    setCentralWidget(centralWidget);

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
