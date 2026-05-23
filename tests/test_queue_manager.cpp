#include "test_queue_manager.h"

#include "core/QueueManager.h"

#include <QtTest/QtTest>

using spray::core::QueueManager;
using spray::protocol::PlcDequeueFrame;

class QueueManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void enqueuePairCreatesBothArms()
    {
        QueueManager queue;
        queue.enqueuePair(10, 1);

        const auto arm1 = queue.itemFor(1, 1);
        const auto arm2 = queue.itemFor(2, 1);

        QVERIFY(arm1.has_value());
        QVERIFY(arm2.has_value());
        QCOMPARE(arm1->count, 10);
        QCOMPARE(arm2->count, 10);
        QCOMPARE(arm1->payload, QByteArray("(1000,10)E"));
        QCOMPARE(arm2->payload, QByteArray("(2000,10)E"));
    }

    void changedPointerCreatesArmTask()
    {
        QueueManager queue;
        queue.enqueuePair(10, 1);

        const auto tasks = queue.handleDequeuePointers(PlcDequeueFrame{1, 0});

        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().armId, 1);
        QCOMPARE(tasks.first().pointer, 1);
        QCOMPARE(tasks.first().count, 10);
        QVERIFY(tasks.first().defaultNoop);
        QVERIFY(queue.cacheFor(1).has_value());
    }

    void repeatedPointerDoesNotCreateDuplicateTask()
    {
        QueueManager queue;
        queue.enqueuePair(10, 1);

        QCOMPARE(queue.handleDequeuePointers(PlcDequeueFrame{1, 0}).size(), 1);
        QCOMPARE(queue.handleDequeuePointers(PlcDequeueFrame{1, 0}).size(), 0);
    }

    void zeroPointerSkipsArm()
    {
        QueueManager queue;
        queue.enqueuePair(10, 1);

        const auto tasks = queue.handleDequeuePointers(PlcDequeueFrame{0, 0});

        QCOMPARE(tasks.size(), 0);
        QVERIFY(!queue.cacheFor(1).has_value());
        QVERIFY(!queue.cacheFor(2).has_value());
    }

    void eachArmChangesIndependently()
    {
        QueueManager queue;
        queue.enqueuePair(10, 1);

        QCOMPARE(queue.handleDequeuePointers(PlcDequeueFrame{1, 0}).size(), 1);
        const auto tasks = queue.handleDequeuePointers(PlcDequeueFrame{1, 1});

        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().armId, 2);
    }

    void missingPointerCreatesDefaultNoop()
    {
        QueueManager queue;

        const auto tasks = queue.handleDequeuePointers(PlcDequeueFrame{5, 0});

        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().armId, 1);
        QCOMPARE(tasks.first().count, 0);
        QCOMPARE(tasks.first().payload, QByteArray("(1000,0)E"));
        QVERIFY(tasks.first().defaultNoop);
    }

    void cameraDataReplacesDefaultPayload()
    {
        QueueManager queue;
        queue.enqueuePair(10, 1);

        QVERIFY(queue.storeCameraData(1, 10, 1, QByteArray("(1001,10)E")));
        const auto tasks = queue.handleDequeuePointers(PlcDequeueFrame{1, 0});

        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().payload, QByteArray("(1001,10)E"));
        QVERIFY(!tasks.first().defaultNoop);
    }
};

QObject* createQueueManagerTest()
{
    return new QueueManagerTest();
}

#include "test_queue_manager.moc"
