#include "app/Application.h"

#include "protocol/ByteCodec.h"
#include "protocol/PlcProtocol.h"

#include <QDebug>

namespace spray::app {

Application::Application(config::AppConfig config, bool simulateRobot, QObject* parent)
    : QObject(parent)
    , config_(std::move(config))
    , simulateRobot_(simulateRobot)
{
}

bool Application::initialize(QString* errorMessage)
{
    if (!simulateRobot_ && !config_.fakeRobot.enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("cpp-minimal-loop requires fake robot mode");
        }
        return false;
    }

    queueManager_ = std::make_unique<core::QueueManager>(
        config_.queue.prefetchOffset,
        config_.queue.maxItemsPerArm);
    plcEndpoint_ = std::make_unique<io::PlcEndpoint>(config_.plc);
    fakeRobot_ = std::make_unique<robot::FakeRobotController>(
        config_.fakeRobot.acceptDelayMs,
        config_.fakeRobot.finishDelayMs);
    dequeueCoordinator_ = std::make_unique<core::DequeueCoordinator>(
        queueManager_.get(),
        plcEndpoint_.get(),
        fakeRobot_.get());

    wireEvents();
    return true;
}

bool Application::start(QString* errorMessage)
{
    if (!plcEndpoint_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("application not initialized");
        }
        return false;
    }

    const bool ok = plcEndpoint_->start(errorMessage);
    if (ok) {
        eventLog_.append(
            QStringLiteral("INFO"),
            QStringLiteral("app"),
            QStringLiteral("listening enqueue=%1 dequeue=%2 fake_robot=true")
                .arg(config_.plc.enqueuePort)
                .arg(config_.plc.dequeuePort));
    }
    return ok;
}

void Application::stop()
{
    if (plcEndpoint_) {
        plcEndpoint_->stop();
    }
}

domain::QueueSnapshot Application::queueSnapshot() const
{
    if (!queueManager_) {
        return {};
    }
    return queueManager_->snapshot();
}

QList<diagnostics::EventRecord> Application::events() const
{
    return eventLog_.records();
}

void Application::wireEvents()
{
    connect(plcEndpoint_.get(), &io::PlcEndpoint::rawFrame, this,
            [this](const QString& channel, const QString& direction, const QByteArray& bytes) {
                eventLog_.append(
                    QStringLiteral("INFO"),
                    QStringLiteral("plc.raw"),
                    QStringLiteral("%1 %2 %3").arg(channel, direction, protocol::toHex(bytes)));
            });

    connect(plcEndpoint_.get(), &io::PlcEndpoint::warning, this,
            [this](const QString& message) {
                eventLog_.append(QStringLiteral("WARN"), QStringLiteral("plc"), message);
                qWarning().noquote() << message;
            });

    connect(plcEndpoint_.get(), &io::PlcEndpoint::enqueueFrameReceived, this,
            [this](const QByteArray& bytes) {
                const auto parsed = protocol::parseEnqueueFrame(bytes);
                if (!parsed.hasValue()) {
                    eventLog_.append(QStringLiteral("WARN"), QStringLiteral("plc.enqueue"), parsed.error->message);
                    qWarning().noquote() << parsed.error->message;
                    return;
                }
                queueManager_->enqueuePair(parsed.value->count, parsed.value->pointer);
                eventLog_.append(
                    QStringLiteral("INFO"),
                    QStringLiteral("queue.enqueue"),
                    QStringLiteral("count=%1 pointer=%2").arg(parsed.value->count).arg(parsed.value->pointer));
            });

    connect(plcEndpoint_.get(), &io::PlcEndpoint::dequeueFrameReceived, this,
            [this](const QByteArray& bytes) {
                const auto parsed = protocol::parseDequeueFrame(bytes);
                if (!parsed.hasValue()) {
                    eventLog_.append(QStringLiteral("WARN"), QStringLiteral("plc.dequeue"), parsed.error->message);
                    qWarning().noquote() << parsed.error->message;
                    return;
                }
                dequeueCoordinator_->onDequeueFrame(*parsed.value);
                eventLog_.append(
                    QStringLiteral("INFO"),
                    QStringLiteral("queue.dequeue"),
                    QStringLiteral("arm1Pointer=%1 arm2Pointer=%2")
                        .arg(parsed.value->arm1Pointer)
                        .arg(parsed.value->arm2Pointer));
            });

    connect(dequeueCoordinator_.get(), &core::DequeueCoordinator::taskDispatched, this,
            [this](const core::RobotTask& task) {
                eventLog_.append(
                    QStringLiteral("INFO"),
                    QStringLiteral("robot.dispatch"),
                    QStringLiteral("arm=%1 count=%2 pointer=%3 default=%4")
                        .arg(task.armId)
                        .arg(task.count)
                        .arg(task.pointer)
                        .arg(task.defaultNoop));
            });

    connect(dequeueCoordinator_.get(), &core::DequeueCoordinator::feedbackSent, this,
            [this](const QByteArray& feedback, bool ok) {
                eventLog_.append(
                    ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                    QStringLiteral("plc.feedback"),
                    QStringLiteral("%1 sent=%2").arg(QString::fromLatin1(feedback)).arg(ok));
            });
}

} // namespace spray::app
