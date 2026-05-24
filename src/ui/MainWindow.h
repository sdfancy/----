#pragma once

#include <QMainWindow>
#include <memory>

class QStackedWidget;
class QListWidget;

namespace spray::domain {
struct SystemSnapshot;
}

namespace spray::ui {

class SnapshotProvider;
class HmiCommandPort;
class OverviewPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(std::shared_ptr<SnapshotProvider> snapshotProvider,
               std::shared_ptr<HmiCommandPort> commandPort,
               QWidget* parent = nullptr);

private slots:
    void onSystemSnapshotUpdated(const spray::domain::SystemSnapshot& snapshot);

private:
    void setupUi();
    void createToolBar();

    std::shared_ptr<SnapshotProvider> m_snapshotProvider;
    std::shared_ptr<HmiCommandPort> m_commandPort;

    QListWidget* m_sidebar;
    QStackedWidget* m_pageStack;
    OverviewPage* m_overviewPage;
};

} // namespace spray::ui
