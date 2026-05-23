#include "test_enqueue_workflow.h"

#include "core/EnqueueWorkflow.h"

#include <QtTest/QtTest>

using spray::core::EnqueueWorkflow;
using spray::core::QueueManager;
using spray::protocol::PlcEnqueueFrame;

namespace {

struct SentCameraCommand {
    QString cameraKey;
    QByteArray payload;
};

} // namespace

class EnqueueWorkflowTest final : public QObject {
    Q_OBJECT

private slots:
    void runsDualCameraHappyPath()
    {
        QueueManager queue;
        EnqueueWorkflow workflow(&queue);
        QList<SentCameraCommand> cameraCommands;
        QList<QByteArray> feedbacks;
        workflow.setCameraSender([&cameraCommands](const QString& cameraKey, const QByteArray& payload) {
            cameraCommands.append({cameraKey, payload});
            return true;
        });
        workflow.setEnqueueFeedbackSender([&feedbacks](const QByteArray& payload) {
            feedbacks.append(payload);
            return true;
        });

        workflow.handlePlcFrame(PlcEnqueueFrame{11, 10, 1, {}});
        QCOMPARE(cameraCommands.size(), 1);
        QCOMPARE(cameraCommands.last().cameraKey, QStringLiteral("3d"));
        QCOMPARE(cameraCommands.last().payload, QByteArray("11,10"));
        QVERIFY(queue.itemFor(1, 1).has_value());
        QVERIFY(queue.itemFor(2, 1).has_value());

        workflow.handleCameraPayload(QStringLiteral("2d"), QByteArray("READY"));
        workflow.handleCameraPayload(QStringLiteral("2d"), QByteArray("READY"));
        QCOMPARE(cameraCommands.size(), 2);
        QCOMPARE(cameraCommands.last().cameraKey, QStringLiteral("2d"));
        QCOMPARE(cameraCommands.last().payload, QByteArray("(10)E"));

        workflow.handleCameraPayload(QStringLiteral("2d"), QByteArray("10,01,02"));
        QCOMPARE(cameraCommands.size(), 3);
        QCOMPARE(cameraCommands.last().cameraKey, QStringLiteral("2d"));
        QCOMPARE(cameraCommands.last().payload, QByteArray("OK"));

        workflow.handlePlcFrame(PlcEnqueueFrame{12, 10, 1, {}});
        QCOMPARE(cameraCommands.size(), 4);
        QCOMPARE(cameraCommands.last().cameraKey, QStringLiteral("3d"));
        QCOMPARE(cameraCommands.last().payload, QByteArray("12,10,01,02"));

        workflow.handleCameraPayload(QStringLiteral("3d"), QByteArray("(1001,10,a)EA(2001,10,b)E"));
        QCOMPARE(feedbacks.size(), 1);
        QCOMPARE(feedbacks.last(), QByteArray("Done"));
        QCOMPARE(queue.itemFor(1, 1)->payload, QByteArray("(1001,10,a)E"));
        QCOMPARE(queue.itemFor(2, 1)->payload, QByteArray("(2001,10,b)E"));
    }

    void fallsBackWhen2dResultIsMissing()
    {
        QueueManager queue;
        EnqueueWorkflow workflow(&queue);
        QList<SentCameraCommand> cameraCommands;
        QList<QByteArray> feedbacks;
        workflow.setCameraSender([&cameraCommands](const QString& cameraKey, const QByteArray& payload) {
            cameraCommands.append({cameraKey, payload});
            return true;
        });
        workflow.setEnqueueFeedbackSender([&feedbacks](const QByteArray& payload) {
            feedbacks.append(payload);
            return true;
        });

        workflow.handlePlcFrame(PlcEnqueueFrame{11, 11, 2, {}});
        workflow.handlePlcFrame(PlcEnqueueFrame{12, 11, 2, {}});
        QCOMPARE(cameraCommands.size(), 2);
        QCOMPARE(cameraCommands.last().payload, QByteArray("12,11,00,00"));

        workflow.handleCameraPayload(QStringLiteral("2d"), QByteArray("11,05,06"));
        QCOMPARE(cameraCommands.size(), 3);
        QCOMPARE(cameraCommands.last().payload, QByteArray("OK"));

        workflow.handleCameraPayload(QStringLiteral("3d"), QByteArray("(1001,11,a)EA(2001,11,b)E"));
        QCOMPARE(feedbacks.size(), 1);
        QCOMPARE(feedbacks.last(), QByteArray("Done"));
    }

    void waitsFor3dHalfPacket()
    {
        QueueManager queue;
        EnqueueWorkflow workflow(&queue);
        QList<SentCameraCommand> cameraCommands;
        QList<QByteArray> feedbacks;
        workflow.setCameraSender([&cameraCommands](const QString& cameraKey, const QByteArray& payload) {
            cameraCommands.append({cameraKey, payload});
            return true;
        });
        workflow.setEnqueueFeedbackSender([&feedbacks](const QByteArray& payload) {
            feedbacks.append(payload);
            return true;
        });

        workflow.handlePlcFrame(PlcEnqueueFrame{11, 12, 3, {}});
        workflow.handlePlcFrame(PlcEnqueueFrame{12, 12, 3, {}});
        workflow.handleCameraPayload(QStringLiteral("3d"), QByteArray("(1001,12,a)EA"));
        QCOMPARE(feedbacks.size(), 0);

        workflow.handleCameraPayload(QStringLiteral("3d"), QByteArray("(2001,12,b)E"));
        QCOMPARE(feedbacks.size(), 1);
        QCOMPARE(feedbacks.last(), QByteArray("Done"));
    }

    void sendsDoneOnlyOnce()
    {
        QueueManager queue;
        EnqueueWorkflow workflow(&queue);
        QList<SentCameraCommand> cameraCommands;
        QList<QByteArray> feedbacks;
        workflow.setCameraSender([&cameraCommands](const QString& cameraKey, const QByteArray& payload) {
            cameraCommands.append({cameraKey, payload});
            return true;
        });
        workflow.setEnqueueFeedbackSender([&feedbacks](const QByteArray& payload) {
            feedbacks.append(payload);
            return true;
        });

        workflow.handlePlcFrame(PlcEnqueueFrame{11, 13, 4, {}});
        workflow.handlePlcFrame(PlcEnqueueFrame{12, 13, 4, {}});
        workflow.handleCameraPayload(QStringLiteral("3d"), QByteArray("(1001,13,a)EA(2001,13,b)E"));
        workflow.handleCameraPayload(QStringLiteral("3d"), QByteArray("(1001,13,a)EA(2001,13,b)E"));

        QCOMPARE(feedbacks.size(), 1);
        QCOMPARE(feedbacks.last(), QByteArray("Done"));
    }
};

QObject* createEnqueueWorkflowTest()
{
    return new EnqueueWorkflowTest();
}

#include "test_enqueue_workflow.moc"
