#include "test_plc_endpoint.h"

#include "io/plc/PlcEndpoint.h"

#include <QTcpSocket>
#include <QtTest/QtTest>

using spray::config::PlcConfig;
using spray::io::PlcEndpoint;

class PlcEndpointTest final : public QObject {
    Q_OBJECT

private slots:
    void receivesRawFramesAndSendsFeedback()
    {
        PlcConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.enqueuePort = 19099;
        config.dequeuePort = 19090;

        PlcEndpoint endpoint(config);
        QString error;
        QVERIFY2(endpoint.start(&error), qPrintable(error));

        QSignalSpy enqueueSpy(&endpoint, &PlcEndpoint::enqueueFrameReceived);
        QSignalSpy dequeueSpy(&endpoint, &PlcEndpoint::dequeueFrameReceived);
        QSignalSpy rawSpy(&endpoint, &PlcEndpoint::rawFrame);

        QTcpSocket enqueueSocket;
        enqueueSocket.connectToHost(config.host, config.enqueuePort);
        QVERIFY(enqueueSocket.waitForConnected(1000));
        enqueueSocket.write(QByteArray::fromHex("0001000A0001"));
        QVERIFY(enqueueSocket.waitForBytesWritten(1000));
        QVERIFY(enqueueSpy.wait(1000));
        QCOMPARE(enqueueSpy.first().at(0).toByteArray(), QByteArray::fromHex("0001000A0001"));

        QTcpSocket dequeueSocket;
        dequeueSocket.connectToHost(config.host, config.dequeuePort);
        QVERIFY(dequeueSocket.waitForConnected(1000));
        dequeueSocket.write(QByteArray::fromHex("00010000"));
        QVERIFY(dequeueSocket.waitForBytesWritten(1000));
        QVERIFY(dequeueSpy.wait(1000));
        QCOMPARE(dequeueSpy.first().at(0).toByteArray(), QByteArray::fromHex("00010000"));

        QVERIFY(endpoint.sendDequeueFeedback(QByteArray("1N")));
        QVERIFY(dequeueSocket.waitForReadyRead(1000));
        QCOMPARE(dequeueSocket.readAll(), QByteArray("1N"));
        QVERIFY(rawSpy.count() >= 3);

        endpoint.stop();
    }

    void warnsWhenFeedbackHasNoConnection()
    {
        PlcConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.enqueuePort = 19199;
        config.dequeuePort = 19190;

        PlcEndpoint endpoint(config);
        QString error;
        QVERIFY2(endpoint.start(&error), qPrintable(error));

        QSignalSpy warningSpy(&endpoint, &PlcEndpoint::warning);
        QVERIFY(!endpoint.sendDequeueFeedback(QByteArray("1D")));
        QCOMPARE(warningSpy.count(), 1);

        endpoint.stop();
    }
};

QObject* createPlcEndpointTest()
{
    return new PlcEndpointTest();
}

#include "test_plc_endpoint.moc"
