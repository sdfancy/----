#include "app/Application.h"

#include "diagnostics/DiagnosticCodes.h"
#include "protocol/ByteCodec.h"
#include "robot/MotionRecipeTable.h"
#include "protocol/PlcProtocol.h"
#include "robot/duco/DucoClientFactory.h"
#include "robot/duco/DucoRobotController.h"

#include <QDebug>

namespace spray::app {
namespace {

struct EventContext {
    int count = -1;
    int pointer = -1;
};

QString plcDevice(const QString& channel)
{
    return QStringLiteral("plc.%1").arg(channel);
}

QString cameraDevice(const QString& cameraKey)
{
    return QStringLiteral("camera.%1").arg(cameraKey);
}

EventContext plcRawContext(const QString& channel, const QString& direction, const QByteArray& bytes)
{
    if (direction != QStringLiteral("rx")) {
        return {};
    }
    if (channel == QStringLiteral("enqueue")) {
        const auto parsed = protocol::parseEnqueueFrame(bytes);
        if (parsed.hasValue()) {
            return {
                static_cast<int>(parsed.value->count),
                static_cast<int>(parsed.value->pointer),
            };
        }
    }
    if (channel == QStringLiteral("dequeue")) {
        const auto parsed = protocol::parseDequeueFrame(bytes);
        if (parsed.hasValue()) {
            const int pointer = parsed.value->arm1Pointer != 0
                ? parsed.value->arm1Pointer
                : parsed.value->arm2Pointer;
            return {-1, pointer};
        }
    }
    return {};
}

} // namespace

Application::Application(config::AppConfig config, bool simulateRobot, QObject* parent)
    : QObject(parent)
    , config_(std::move(config))
    , simulateRobot_(simulateRobot)
{
}

bool Application::initialize(QString* errorMessage)
{
    diagnostics_ = std::make_unique<diagnostics::DiagnosticsService>(config_.logging);
    queueManager_ = std::make_unique<core::QueueManager>(
        config_.queue.prefetchOffset,
        config_.queue.maxItemsPerArm);
    plcEndpoint_ = std::make_unique<io::PlcEndpoint>(config_.plc);
    cameraEndpoint_ = std::make_unique<io::CameraEndpoint>(config_.camera);
    const auto robotMode = simulateRobot_ ? config::RobotMode::Fake : config_.robot.mode;
    if (robotMode == config::RobotMode::Fake) {
        robot_ = std::make_unique<robot::FakeRobotController>(
            config_.fakeRobot.acceptDelayMs,
            config_.fakeRobot.finishDelayMs);
    } else {
        auto factory = robot::duco::createDucoClientFactory(config_.robot);
        auto ducoController = std::make_unique<robot::duco::DucoRobotController>(
            config_.robot,
            std::move(factory));
        QString recipeError;
        const auto recipes = robot::MotionRecipeTable::load(config_.robot.recipePath, &recipeError);
        if (!recipeError.isEmpty()) {
            if (errorMessage) {
                *errorMessage = recipeError;
            }
            return false;
        }
        for (const auto& recipe : recipes.recipes()) {
            ducoController->setMotionRecipe(recipe);
        }
        robot_ = std::move(ducoController);
    }
    dequeueCoordinator_ = std::make_unique<core::DequeueCoordinator>(
        queueManager_.get(),
        plcEndpoint_.get(),
        robot_.get());
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

    const auto robotStart = config_.robot.prepareOnStart
        ? robot_->prepare()
        : robot_->connectRobot();
    if (!robotStart.ok) {
        if (errorMessage) {
            *errorMessage = robotStart.message;
        }
        return false;
    }

    if (!cameraEndpoint_->start(errorMessage)) {
        robot_->disconnectRobot();
        return false;
    }

    const bool ok = plcEndpoint_->start(errorMessage);
    if (!ok) {
        robot_->disconnectRobot();
        cameraEndpoint_->stop();
        return false;
    }

    if (ok) {
        diagnostics_->recordMessage(
            QStringLiteral("INFO"),
            QStringLiteral("app"),
            QStringLiteral("listening enqueue=%1 dequeue=%2 robot_mode=%3 camera_mode=%4")
                .arg(config_.plc.enqueuePort)
                .arg(config_.plc.dequeuePort)
                .arg(simulateRobot_ || config_.robot.mode == config::RobotMode::Fake
                         ? QStringLiteral("fake")
                         : QStringLiteral("duco"))
                .arg(config_.camera.flowMode == config::CameraFlowMode::DualCamera11_12
                         ? QStringLiteral("dual_camera_11_12")
                         : QStringLiteral("legacy_single_camera")));
    }
    return true;
}

void Application::stop()
{
    if (robot_) {
        robot_->disconnectRobot();
    }
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
    if (!diagnostics_) {
        return {};
    }
    return diagnostics_->events();
}

QList<diagnostics::EventRecord> Application::events(const diagnostics::EventFilter& filter) const
{
    if (!diagnostics_) {
        return {};
    }
    return diagnostics_->events(filter);
}

QList<domain::DeviceHealthSnapshot> Application::deviceHealthSnapshot() const
{
    if (!diagnostics_) {
        return {};
    }
    return diagnostics_->deviceHealthSnapshot();
}

bool Application::flushDiagnostics(QString* errorMessage)
{
    if (!diagnostics_) {
        return true;
    }
    return diagnostics_->flush(errorMessage);
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
                const auto context = plcRawContext(channel, direction, bytes);
                const int connectionCount = channel == QStringLiteral("enqueue")
                    ? plcEndpoint_->enqueueConnectionCount()
                    : plcEndpoint_->dequeueConnectionCount();
                diagnostics_->recordRawFrame(
                    QStringLiteral("plc.raw"),
                    plcDevice(channel),
                    direction,
                    bytes,
                    context.count,
                    context.pointer,
                    connectionCount);
            });

    connect(plcEndpoint_.get(), &io::PlcEndpoint::warning, this,
            [this](const QString& message) {
                diagnostics_->recordWarning(QStringLiteral("plc"), QStringLiteral("plc"), message);
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
                    diagnostics_->recordWarning(
                        QStringLiteral("plc.dequeue"),
                        QStringLiteral("plc.dequeue"),
                        parsed.error->message,
                        diagnostics::DiagnosticCode::plcProtocolInvalidFrame());
                    qWarning().noquote() << parsed.error->message;
                    return;
                }
                dequeueCoordinator_->onDequeueFrame(*parsed.value);
                diagnostics_->recordMessage(
                    QStringLiteral("INFO"),
                    QStringLiteral("queue.dequeue"),
                    QStringLiteral("arm1Pointer=%1 arm2Pointer=%2")
                        .arg(parsed.value->arm1Pointer)
                        .arg(parsed.value->arm2Pointer),
                    QStringLiteral("queue"),
                    QString(),
                    -1,
                    parsed.value->arm1Pointer != 0
                        ? parsed.value->arm1Pointer
                        : parsed.value->arm2Pointer);
            });
}

void Application::wireDequeueCoordinatorEvents()
{
    connect(dequeueCoordinator_.get(), &core::DequeueCoordinator::taskDispatched, this,
            [this](const core::RobotTask& task) {
                diagnostics_->recordMessage(
                    QStringLiteral("INFO"),
                    QStringLiteral("robot.dispatch"),
                    QStringLiteral("arm=%1 count=%2 pointer=%3 default=%4")
                        .arg(task.armId)
                        .arg(task.count)
                        .arg(task.pointer)
                        .arg(task.defaultNoop),
                    QStringLiteral("robot"),
                    QString(),
                    task.count,
                    task.pointer);
            });

    connect(dequeueCoordinator_.get(), &core::DequeueCoordinator::feedbackSent, this,
            [this](const QByteArray& feedback, bool ok) {
                diagnostics_->recordMessage(
                    ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                    QStringLiteral("plc.feedback"),
                    QStringLiteral("%1 sent=%2").arg(QString::fromLatin1(feedback)).arg(ok),
                    QStringLiteral("plc.dequeue"));
            });

    connect(robot_.get(), &robot::IRobotController::warning, this,
            [this](const QString& message) {
                diagnostics_->recordWarning(
                    QStringLiteral("robot"),
                    QStringLiteral("robot"),
                    message,
                    diagnostics::DiagnosticCode::robotFault());
            });

    connect(robot_.get(), &robot::IRobotController::statusChanged, this,
            [this](const robot::RobotStatus& status) {
                diagnostics_->recordMessage(
                    QStringLiteral("INFO"),
                    QStringLiteral("robot.status"),
                    QStringLiteral("state=%1 moving=%2 message=%3")
                        .arg(static_cast<int>(status.connectionState))
                        .arg(status.moving)
                        .arg(status.message),
                    QStringLiteral("robot"));
            });
}

void Application::wireCameraEvents()
{
    connect(cameraEndpoint_.get(), &io::CameraEndpoint::rawFrame, this,
            [this](const QString& cameraKey, const QString& direction, const QByteArray& payload) {
                diagnostics_->recordRawFrame(
                    QStringLiteral("camera.raw"),
                    cameraDevice(cameraKey),
                    direction,
                    payload,
                    -1,
                    -1,
                    cameraEndpoint_->connectionCount(cameraKey));
            });
    connect(cameraEndpoint_.get(), &io::CameraEndpoint::warning, this,
            [this](const QString& message) {
                diagnostics_->recordWarning(
                    QStringLiteral("camera"),
                    QStringLiteral("camera"),
                    message,
                    diagnostics::DiagnosticCode::cameraWarning());
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
                    diagnostics_->recordMessage(
                        ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                        QStringLiteral("camera.command"),
                        QStringLiteral("%1 %2 sent=%3").arg(cameraKey, QString::fromLatin1(payload)).arg(ok),
                        cameraDevice(cameraKey));
                });
        connect(enqueueWorkflow_.get(), &core::EnqueueWorkflow::enqueueFeedbackSent, this,
                [this](const QByteArray& payload, bool ok) {
                    diagnostics_->recordMessage(
                        ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                        QStringLiteral("plc.enqueue.feedback"),
                        QStringLiteral("%1 sent=%2").arg(QString::fromLatin1(payload)).arg(ok),
                        QStringLiteral("plc.enqueue"));
                });
        connect(enqueueWorkflow_.get(), &core::EnqueueWorkflow::warning, this,
                [this](const QString& message) {
                    diagnostics_->recordWarning(
                        QStringLiteral("enqueue.workflow"),
                        QStringLiteral("camera.workflow"),
                        message,
                        diagnostics::DiagnosticCode::cameraWarning());
                });
    }

    if (legacyCameraWorkflow_) {
        connect(legacyCameraWorkflow_.get(), &core::LegacyCameraWorkflow::cameraTriggerSent, this,
                [this](const QByteArray& payload, bool ok) {
                    diagnostics_->recordMessage(
                        ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                        QStringLiteral("legacy.camera.trigger"),
                        QStringLiteral("%1 sent=%2").arg(protocol::toHex(payload)).arg(ok),
                        QStringLiteral("camera.legacy"));
                });
        connect(legacyCameraWorkflow_.get(), &core::LegacyCameraWorkflow::stageFeedbackSent, this,
                [this](const QByteArray& payload, bool ok) {
                    diagnostics_->recordMessage(
                        ok ? QStringLiteral("INFO") : QStringLiteral("WARN"),
                        QStringLiteral("plc.enqueue.feedback"),
                        QStringLiteral("%1 sent=%2").arg(QString::fromLatin1(payload)).arg(ok),
                        QStringLiteral("plc.enqueue"));
                });
        connect(legacyCameraWorkflow_.get(), &core::LegacyCameraWorkflow::warning, this,
                [this](const QString& message) {
                    diagnostics_->recordWarning(
                        QStringLiteral("legacy.camera.workflow"),
                        QStringLiteral("camera.legacy"),
                        message,
                        diagnostics::DiagnosticCode::cameraWarning());
                });
    }
}

void Application::handlePlcEnqueueFrame(const QByteArray& bytes)
{
    const auto parsed = protocol::parseEnqueueFrame(bytes);
    if (!parsed.hasValue()) {
        diagnostics_->recordWarning(
            QStringLiteral("plc.enqueue"),
            QStringLiteral("plc.enqueue"),
            parsed.error->message,
            diagnostics::DiagnosticCode::plcProtocolInvalidFrame());
        qWarning().noquote() << parsed.error->message;
        return;
    }

    diagnostics_->recordMessage(
        QStringLiteral("INFO"),
        QStringLiteral("queue.enqueue"),
        QStringLiteral("command=%1 count=%2 pointer=%3")
            .arg(parsed.value->command)
            .arg(parsed.value->count)
            .arg(parsed.value->pointer),
        QStringLiteral("queue"),
        QString(),
        parsed.value->count,
        parsed.value->pointer);

    if (enqueueWorkflow_) {
        enqueueWorkflow_->handlePlcFrame(*parsed.value);
        return;
    }
    if (legacyCameraWorkflow_) {
        legacyCameraWorkflow_->handlePlcFrame(*parsed.value);
    }
}

} // namespace spray::app
