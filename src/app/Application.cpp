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
    cameraEndpoint_ = std::make_unique<io::CameraEndpoint>(config_.camera);
    fakeRobot_ = std::make_unique<robot::FakeRobotController>(
        config_.fakeRobot.acceptDelayMs,
        config_.fakeRobot.finishDelayMs);
    dequeueCoordinator_ = std::make_unique<core::DequeueCoordinator>(
        queueManager_.get(),
        plcEndpoint_.get(),
        fakeRobot_.get());
    if (config_.camera.flowMode == config::CameraFlowMode::DualCamera11_12) {
        enqueueWorkflow_ = std::make_unique<core::EnqueueWorkflow>(queueManager_.get());
        enqueueWorkflow_->setCameraSender([this](const QString& cameraKey, const QByteArray& payload) {
            return cameraEndpoint_->sendToCamera(cameraKey, payload);
        });
        enqueueWorkflow_->setEnqueueFeedbackSender([this](const QByteArray& payload) {
            return plcEndpoint_->sendEnqueueFeedback(payload);
        });
    } else {
        legacyCameraWorkflow_ = std::make_unique<core::LegacyCameraWorkflow>(
            queueManager_.get(),
            config_.camera.correlationMode,
            config_.camera.countExtractMode);
        legacyCameraWorkflow_->setCameraTriggerSender([this](const QByteArray& payload) {
            return cameraEndpoint_->sendToCamera(QStringLiteral("legacy"), payload);
        });
        legacyCameraWorkflow_->setStageFeedbackSender([this](const QByteArray& payload) {
            return plcEndpoint_->sendEnqueueFeedback(payload);
        });
    }

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

    if (!cameraEndpoint_->start(errorMessage)) {
        return false;
    }

    const bool ok = plcEndpoint_->start(errorMessage);
    if (!ok) {
        cameraEndpoint_->stop();
        return false;
    }

    if (ok) {
        eventLog_.append(
            QStringLiteral("INFO"),
            QStringLiteral("app"),
            QStringLiteral("listening enqueue=%1 dequeue=%2 fake_robot=true camera_mode=%3")
                .arg(config_.plc.enqueuePort)
                .arg(config_.plc.dequeuePort)
                .arg(config_.camera.flowMode == config::CameraFlowMode::DualCamera11_12
                         ? QStringLiteral("dual_camera_11_12")
                         : QStringLiteral("legacy_single_camera")));
    }
    return true;
}

void Application::stop()
{
    if (cameraEndpoint_) {
        cameraEndpoint_->stop();
    }
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
    wireDiagnosticEvents();
    wirePlcEnqueueEvents();
    wirePlcDequeueEvents();
    wireDequeueCoordinatorEvents();
    wireCameraEvents();
    wireWorkflowEvents();
}

void Application::wireDiagnosticEvents()
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
}

void Application::wirePlcEnqueueEvents()
{
    connect(plcEndpoint_.get(), &io::PlcEndpoint::enqueueFrameReceived, this,
            [this](const QByteArray& bytes) {
                handlePlcEnqueueFrame(bytes);
            });
}

void Application::wirePlcDequeueEvents()
{
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
}

void Application::wireDequeueCoordinatorEvents()
{
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

void Application::wireCameraEvents()
{
    connect(cameraEndpoint_.get(), &io::CameraEndpoint::rawFrame, this,
            [this](const QString& cameraKey, const QString& direction, const QByteArray& payload) {
                eventLog_.append(
                    QStringLiteral("INFO"),
                    QStringLiteral("camera.raw"),
                    QStringLiteral("%1 %2 %3").arg(cameraKey, direction, protocol::toHex(payload)));
            });
    connect(cameraEndpoint_.get(), &io::CameraEndpoint::warning, this,
            [this](const QString& message) {
                eventLog_.append(QStringLiteral("WARN"), QStringLiteral("camera"), message);
                qWarning().noquote() << message;
            });
    connect(cameraEndpoint_.get(), &io::CameraEndpoint::payloadReceived, this,
            [this](const QString& cameraKey, const QByteArray& payload) {
                if (enqueueWorkflow_) {
                    enqueueWorkflow_->handleCameraPayload(cameraKey, payload);
                    return;
                }
                if (legacyCameraWorkflow_) {
                    legacyCameraWorkflow_->handleCameraPayload(payload);
                }
            });
}

void Application::wireWorkflowEvents()
{
    if (enqueueWorkflow_) {
        connect(enqueueWorkflow_.get(), &core::EnqueueWorkflow::cameraCommandSent, this,
                [this](const QString& cameraKey, const QByteArray& payload, bool ok) {
                    eventLog_.append(
                        ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                        QStringLiteral("camera.command"),
                        QStringLiteral("%1 %2 sent=%3").arg(cameraKey, QString::fromLatin1(payload)).arg(ok));
                });
        connect(enqueueWorkflow_.get(), &core::EnqueueWorkflow::enqueueFeedbackSent, this,
                [this](const QByteArray& payload, bool ok) {
                    eventLog_.append(
                        ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                        QStringLiteral("plc.enqueue.feedback"),
                        QStringLiteral("%1 sent=%2").arg(QString::fromLatin1(payload)).arg(ok));
                });
        connect(enqueueWorkflow_.get(), &core::EnqueueWorkflow::warning, this,
                [this](const QString& message) {
                    eventLog_.append(QStringLiteral("WARN"), QStringLiteral("enqueue.workflow"), message);
                });
    }

    if (legacyCameraWorkflow_) {
        connect(legacyCameraWorkflow_.get(), &core::LegacyCameraWorkflow::cameraTriggerSent, this,
                [this](const QByteArray& payload, bool ok) {
                    eventLog_.append(
                        ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                        QStringLiteral("legacy.camera.trigger"),
                        QStringLiteral("%1 sent=%2").arg(protocol::toHex(payload)).arg(ok));
                });
        connect(legacyCameraWorkflow_.get(), &core::LegacyCameraWorkflow::stageFeedbackSent, this,
                [this](const QByteArray& payload, bool ok) {
                    eventLog_.append(
                        ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                        QStringLiteral("plc.enqueue.feedback"),
                        QStringLiteral("%1 sent=%2").arg(QString::fromLatin1(payload)).arg(ok));
                });
        connect(legacyCameraWorkflow_.get(), &core::LegacyCameraWorkflow::warning, this,
                [this](const QString& message) {
                    eventLog_.append(QStringLiteral("WARN"), QStringLiteral("legacy.camera.workflow"), message);
                });
    }
}

void Application::handlePlcEnqueueFrame(const QByteArray& bytes)
{
    const auto parsed = protocol::parseEnqueueFrame(bytes);
    if (!parsed.hasValue()) {
        eventLog_.append(QStringLiteral("WARN"), QStringLiteral("plc.enqueue"), parsed.error->message);
        qWarning().noquote() << parsed.error->message;
        return;
    }

    eventLog_.append(
        QStringLiteral("INFO"),
        QStringLiteral("queue.enqueue"),
        QStringLiteral("command=%1 count=%2 pointer=%3")
            .arg(parsed.value->command)
            .arg(parsed.value->count)
            .arg(parsed.value->pointer));

    if (enqueueWorkflow_) {
        enqueueWorkflow_->handlePlcFrame(*parsed.value);
        return;
    }
    if (legacyCameraWorkflow_) {
        legacyCameraWorkflow_->handlePlcFrame(*parsed.value);
    }
}

} // namespace spray::app
