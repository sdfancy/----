#include "test_legacy_camera_workflow.h"

#include "core/LegacyCameraWorkflow.h"

#include <QtTest/QtTest>

using spray::config::CameraCorrelationMode;
using spray::config::CameraCountExtractMode;
using spray::core::LegacyCameraWorkflow;
using spray::core::QueueManager;
using spray::protocol::PlcEnqueueFrame;

class LegacyCameraWorkflowTest final : public QObject {
    Q_OBJECT

private slots:
    void sequentialModeTriggersAndStoresByWaitingSlot()
    {
        QueueManager queue;
        LegacyCameraWorkflow workflow(
            &queue,
            CameraCorrelationMode::Sequential,
            CameraCountExtractMode::Auto);
        QList<QByteArray> triggers;
        QList<QByteArray> feedbacks;
        workflow.setCameraTriggerSender([&triggers](const QByteArray& payload) {
            triggers.append(payload);
            return true;
        });
        workflow.setStageFeedbackSender([&feedbacks](const QByteArray& payload) {
            feedbacks.append(payload);
            return true;
        });

        workflow.handlePlcFrame(PlcEnqueueFrame{1, 10, 1, {}});
        QCOMPARE(triggers.size(), 1);
        QCOMPARE(triggers.last(), QByteArray::fromHex("0001000A"));

        workflow.handleCameraPayload(QByteArray("(1001,a)E"));
        QCOMPARE(queue.itemFor(1, 1)->payload, QByteArray("(1001,10,a)E"));
        QCOMPARE(feedbacks.last(), QByteArray("1Done"));

        workflow.handlePlcFrame(PlcEnqueueFrame{11, 10, 1, {}});
        QCOMPARE(triggers.size(), 2);
        QCOMPARE(triggers.last(), QByteArray::fromHex("000B000A"));

        workflow.handleCameraPayload(QByteArray("(2001,b)E"));
        QCOMPARE(queue.itemFor(2, 1)->payload, QByteArray("(2001,10,b)E"));
        QCOMPARE(feedbacks.last(), QByteArray("2Done"));
    }

    void countedModeStoresOutOfOrderPayloadsByCount()
    {
        QueueManager queue;
        LegacyCameraWorkflow workflow(
            &queue,
            CameraCorrelationMode::Counted,
            CameraCountExtractMode::Auto);
        QList<QByteArray> triggers;
        QList<QByteArray> feedbacks;
        workflow.setCameraTriggerSender([&triggers](const QByteArray& payload) {
            triggers.append(payload);
            return true;
        });
        workflow.setStageFeedbackSender([&feedbacks](const QByteArray& payload) {
            feedbacks.append(payload);
            return true;
        });

        workflow.handlePlcFrame(PlcEnqueueFrame{1, 20, 1, {}});
        workflow.handlePlcFrame(PlcEnqueueFrame{1, 21, 2, {}});

        workflow.handleCameraPayload(QByteArray("(1001,20,a)E"));
        workflow.handleCameraPayload(QByteArray("(2001,21,b)E"));

        QCOMPARE(queue.itemFor(1, 1)->payload, QByteArray("(1001,20,a)E"));
        QCOMPARE(queue.itemFor(2, 2)->payload, QByteArray("(2001,21,b)E"));
        QCOMPARE(feedbacks.size(), 2);
        QCOMPARE(feedbacks.at(0), QByteArray("1Done"));
        QCOMPARE(feedbacks.at(1), QByteArray("2Done"));
    }

    void countedModeCanUseAsciiCount()
    {
        QueueManager queue;
        LegacyCameraWorkflow workflow(
            &queue,
            CameraCorrelationMode::Counted,
            CameraCountExtractMode::Ascii);
        QList<QByteArray> triggers;
        QList<QByteArray> feedbacks;
        workflow.setCameraTriggerSender([&triggers](const QByteArray& payload) {
            triggers.append(payload);
            return true;
        });
        workflow.setStageFeedbackSender([&feedbacks](const QByteArray& payload) {
            feedbacks.append(payload);
            return true;
        });

        workflow.handlePlcFrame(PlcEnqueueFrame{1, 30, 3, {}});
        workflow.handleCameraPayload(QByteArray("(1001,COUNT:30,a)E"));

        QCOMPARE(queue.itemFor(1, 3)->payload, QByteArray("(1001,COUNT:30,a)E"));
        QCOMPARE(feedbacks.size(), 1);
        QCOMPARE(feedbacks.last(), QByteArray("1Done"));
    }
};

QObject* createLegacyCameraWorkflowTest()
{
    return new LegacyCameraWorkflowTest();
}

#include "test_legacy_camera_workflow.moc"
