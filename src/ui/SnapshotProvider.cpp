#include "SnapshotProvider.h"
#include "app/Application.h"

namespace spray::ui {

SnapshotProvider::SnapshotProvider(std::shared_ptr<spray::app::Application> app, QObject* parent)
    : QObject(parent), m_app(std::move(app)), m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &SnapshotProvider::buildAndEmitSnapshot);
}

SnapshotProvider::~SnapshotProvider() = default;

void SnapshotProvider::start(int intervalMs)
{
    m_timer->start(intervalMs);
    buildAndEmitSnapshot(); // emit immediately on start
}

void SnapshotProvider::stop()
{
    m_timer->stop();
}

void SnapshotProvider::buildAndEmitSnapshot()
{
    if (!m_app) return;

    spray::domain::SystemSnapshot snapshot;
    // For now, we mock the retrieval since Application might not have the full getters yet.
    // In a full implementation, we would do: snapshot.queue = m_app->queueSnapshot();
    // For demonstration of the UI, we just emit an empty or partially filled snapshot.

    emit systemSnapshotUpdated(snapshot);
}

} // namespace spray::ui
