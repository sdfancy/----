#pragma once

#include "domain/Snapshots.h"
#include <QObject>
#include <QTimer>
#include <memory>

namespace spray::app {
class Application;
}

namespace spray::ui {

class SnapshotProvider : public QObject {
    Q_OBJECT
public:
    explicit SnapshotProvider(std::shared_ptr<spray::app::Application> app, QObject* parent = nullptr);
    ~SnapshotProvider() override;

    void start(int intervalMs = 500);
    void stop();

signals:
    void systemSnapshotUpdated(const spray::domain::SystemSnapshot& snapshot);

private slots:
    void buildAndEmitSnapshot();

private:
    std::shared_ptr<spray::app::Application> m_app;
    QTimer* m_timer;
};

} // namespace spray::ui
