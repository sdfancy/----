#include "test_camera_endpoint.h"

#include "io/camera/CameraEndpoint.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

using spray::config::CameraFlowMode;
using spray::io::CameraEndpoint;

class CameraEndpointTest final : public QObject {
    Q_OBJECT

private slots:
    void dualModeAccepts2dAnd3dClients()
    {
        spray::config::CameraConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.camera2dPort = 0;
        config.camera3dPort = 0;
        config.camera3dEnabled = true;
        config.flowMode = CameraFlowMode::DualCamera11_12;

        CameraEndpoint endpoint(config);
        QString error;
        QVERIFY2(endpoint.start(&error), qPrintable(error));
        QVERIFY(endpoint.listeningPort(QStringLiteral("2d")) > 0);
        QVERIFY(endpoint.listeningPort(QStringLiteral("3d")) > 0);

        QSignalSpy payloadSpy(&endpoint, &CameraEndpoint::payloadReceived);
        QTcpSocket camera2d;
        QTcpSocket camera3d;
        camera2d.connectToHost(config.host, endpoint.listeningPort(QStringLiteral("2d")));
        camera3d.connectToHost(config.host, endpoint.listeningPort(QStringLiteral("3d")));
        QVERIFY(camera2d.waitForConnected(1000));
        QVERIFY(camera3d.waitForConnected(1000));
        QTRY_COMPARE(endpoint.connectionCount(QStringLiteral("2d")), 1);
        QTRY_COMPARE(endpoint.connectionCount(QStringLiteral("3d")), 1);

        camera2d.write(QByteArray("READY"));
        camera3d.write(QByteArray("(1001,10,a)EA(2001,10,b)E"));
        QVERIFY(camera2d.waitForBytesWritten(1000));
        QVERIFY(camera3d.waitForBytesWritten(1000));
        QTRY_COMPARE(payloadSpy.count(), 2);
        QCOMPARE(payloadSpy.at(0).at(0).toString(), QStringLiteral("2d"));
        QCOMPARE(payloadSpy.at(0).at(1).toByteArray(), QByteArray("READY"));
        QCOMPARE(payloadSpy.at(1).at(0).toString(), QStringLiteral("3d"));

        QVERIFY(endpoint.sendToCamera(QStringLiteral("2d"), QByteArray("(10)E")));
        QVERIFY(camera2d.waitForReadyRead(1000));
        QCOMPARE(camera2d.readAll(), QByteArray("(10)E"));

        QVERIFY(endpoint.sendToCamera(QStringLiteral("3d"), QByteArray("11,10")));
        QVERIFY(camera3d.waitForReadyRead(1000));
        QCOMPARE(camera3d.readAll(), QByteArray("11,10"));
    }

    void legacyModeConnectsAndSendsTrigger()
    {
        QTcpServer cameraServer;
        QVERIFY(cameraServer.listen(QHostAddress::LocalHost, 0));

        spray::config::CameraConfig config;
        config.legacyHost = QStringLiteral("127.0.0.1");
        config.legacyPort = cameraServer.serverPort();
        config.flowMode = CameraFlowMode::LegacySingleCamera;

        CameraEndpoint endpoint(config);
        QSignalSpy payloadSpy(&endpoint, &CameraEndpoint::payloadReceived);
        QString error;
        QVERIFY2(endpoint.start(&error), qPrintable(error));
        QVERIFY(cameraServer.waitForNewConnection(1000));
        QTcpSocket* camera = cameraServer.nextPendingConnection();
        QVERIFY(camera);
        QTRY_COMPARE(endpoint.connectionCount(QStringLiteral("legacy")), 1);

        QVERIFY(endpoint.sendToCamera(QStringLiteral("legacy"), QByteArray("CMD:11,COUNT:10")));
        QVERIFY(camera->waitForReadyRead(1000));
        QCOMPARE(camera->readAll(), QByteArray("CMD:11,COUNT:10"));

        camera->write(QByteArray("COUNT:10"));
        QVERIFY(camera->waitForBytesWritten(1000));
        QTRY_COMPARE(payloadSpy.count(), 1);
        QCOMPARE(payloadSpy.at(0).at(0).toString(), QStringLiteral("legacy"));
        QCOMPARE(payloadSpy.at(0).at(1).toByteArray(), QByteArray("COUNT:10"));
    }

    void legacyModeDoesNotListenOnDualPorts()
    {
        QTcpServer cameraServer;
        QVERIFY(cameraServer.listen(QHostAddress::LocalHost, 0));

        spray::config::CameraConfig config;
        config.legacyHost = QStringLiteral("127.0.0.1");
        config.legacyPort = cameraServer.serverPort();
        config.camera2dPort = 0;
        config.camera3dPort = 0;
        config.camera3dEnabled = true;
        config.flowMode = CameraFlowMode::LegacySingleCamera;

        CameraEndpoint endpoint(config);
        QString error;
        QVERIFY2(endpoint.start(&error), qPrintable(error));
        QCOMPARE(endpoint.listeningPort(QStringLiteral("2d")), 0);
        QCOMPARE(endpoint.listeningPort(QStringLiteral("3d")), 0);
    }
};

QObject* createCameraEndpointTest()
{
    return new CameraEndpointTest();
}

#include "test_camera_endpoint.moc"
