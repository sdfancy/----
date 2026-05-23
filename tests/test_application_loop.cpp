#include "test_application_loop.h"

#include "app/Application.h"

#include <QTcpSocket>
#include <QtTest/QtTest>

using spray::app::Application;
using spray::config::AppConfig;

class ApplicationLoopTest final : public QObject {
    Q_OBJECT

private slots:
    void tracksLoopInEventsAndSnapshot()
    {
        AppConfig config;
        config.plc.host = QStringLiteral("127.0.0.1");
        config.plc.enqueuePort = 19499;
        config.plc.dequeuePort = 19490;
        config.fakeRobot.acceptDelayMs = 1;
        config.fakeRobot.finishDelayMs = 1;

        Application app(config, true);
        QString error;
        QVERIFY2(app.initialize(&error), qPrintable(error));
        QVERIFY2(app.start(&error), qPrintable(error));

        QTcpSocket enqueueSocket;
        enqueueSocket.connectToHost(config.plc.host, config.plc.enqueuePort);
        QVERIFY(enqueueSocket.waitForConnected(1000));
        enqueueSocket.write(QByteArray::fromHex("0001000A0001"));
        QVERIFY(enqueueSocket.waitForBytesWritten(1000));

        QTcpSocket dequeueSocket;
        dequeueSocket.connectToHost(config.plc.host, config.plc.dequeuePort);
        QVERIFY(dequeueSocket.waitForConnected(1000));
        dequeueSocket.write(QByteArray::fromHex("00010000"));
        QVERIFY(dequeueSocket.waitForBytesWritten(1000));

        QTRY_VERIFY(dequeueSocket.bytesAvailable() >= 4);
        QCOMPARE(dequeueSocket.read(4), QByteArray("1N1D"));

        QTRY_VERIFY(app.events().size() >= 7);
        const auto snapshot = app.queueSnapshot();
        QVERIFY(!snapshot.items.isEmpty());

        bool sawPointer = false;
        for (const auto& item : snapshot.items) {
            if (item.armId == 1 && item.pointer == 1 && item.count == 10) {
                sawPointer = true;
            }
        }
        QVERIFY(sawPointer);

        app.stop();
    }
};

QObject* createApplicationLoopTest()
{
    return new ApplicationLoopTest();
}

#include "test_application_loop.moc"
