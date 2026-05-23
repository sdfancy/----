#include "test_camera_protocol.h"

#include "protocol/CameraProtocol.h"

#include <QtTest/QtTest>

using spray::protocol::Parsed2dFrameKind;
using spray::protocol::drain2dFrames;
using spray::protocol::drain3dFrames;
using spray::protocol::parse2dFrame;
using spray::protocol::parse3dSegment;
using spray::protocol::split3dSegments;

class CameraProtocolTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesReadyFrames()
    {
        const QList<QByteArray> frames = {
            QByteArray("READY"),
            QByteArray("(READY)E"),
            QByteArray("READYE"),
        };

        for (const auto& frame : frames) {
            const auto result = parse2dFrame(frame);
            QVERIFY2(result.hasValue(), frame.constData());
            QCOMPARE(result.value->kind, Parsed2dFrameKind::Ready);
        }
    }

    void parses2dResultFrames()
    {
        const auto csv = parse2dFrame(QByteArray("10,01,02"));
        QVERIFY(csv.hasValue());
        QCOMPARE(csv.value->kind, Parsed2dFrameKind::Result);
        QCOMPARE(csv.value->count, 10);
        QCOMPARE(csv.value->partType, QStringLiteral("01,02"));

        const auto wrapped = parse2dFrame(QByteArray("(11,03,04)E"));
        QVERIFY(wrapped.hasValue());
        QCOMPARE(wrapped.value->count, 11);
        QCOMPARE(wrapped.value->partType, QStringLiteral("03,04"));
    }

    void defaults2dPartType()
    {
        const auto result = parse2dFrame(QByteArray("12"));

        QVERIFY(result.hasValue());
        QCOMPARE(result.value->count, 12);
        QCOMPARE(result.value->partType, QStringLiteral("00,00"));
    }

    void drains2dDelimitedFrames()
    {
        const auto drained = drain2dFrames(QByteArray(" \r\nREADY\n(10,01,02)E11,03,04"));

        QCOMPARE(drained.frames.size(), 3);
        QCOMPARE(drained.frames.at(0), QByteArray("READY"));
        QCOMPARE(drained.frames.at(1), QByteArray("(10,01,02)E"));
        QCOMPARE(drained.frames.at(2), QByteArray("11,03,04"));
        QCOMPARE(drained.remaining, QByteArray());
    }

    void keeps2dHalfPacket()
    {
        const auto drained = drain2dFrames(QByteArray("(10,01"));

        QCOMPARE(drained.frames.size(), 0);
        QCOMPARE(drained.remaining, QByteArray("(10,01"));
    }

    void drains3dCompleteFrames()
    {
        const auto drained = drain3dFrames(
            QByteArray("noise(1001,10,a)EA(2001,10,b)E(1001,11,c)EA(2001,11,d)Etail"));

        QCOMPARE(drained.frames.size(), 2);
        QCOMPARE(drained.frames.at(0), QByteArray("(1001,10,a)EA(2001,10,b)E"));
        QCOMPARE(drained.frames.at(1), QByteArray("(1001,11,c)EA(2001,11,d)E"));
        QCOMPARE(drained.remaining, QByteArray("tail"));
    }

    void keeps3dHalfPacket()
    {
        const auto drained = drain3dFrames(QByteArray("(1001,10,a)EA(2001,10,b)"));

        QCOMPARE(drained.frames.size(), 0);
        QCOMPARE(drained.remaining, QByteArray("(1001,10,a)EA(2001,10,b)"));
    }

    void parses3dSegments()
    {
        const QList<QByteArray> segments = split3dSegments(QByteArray("(1001,10,a)EA(2001,10,b)E"));

        QCOMPARE(segments.size(), 2);
        const auto arm1 = parse3dSegment(segments.at(0));
        const auto arm2 = parse3dSegment(segments.at(1));

        QVERIFY(arm1.hasValue());
        QVERIFY(arm2.hasValue());
        QCOMPARE(arm1.value->armId, 1);
        QCOMPARE(arm1.value->count, 10);
        QCOMPARE(arm1.value->payload, QByteArray("(1001,10,a)E"));
        QCOMPARE(arm2.value->armId, 2);
        QCOMPARE(arm2.value->count, 10);
        QCOMPARE(arm2.value->payload, QByteArray("(2001,10,b)E"));
    }

    void rejectsInvalid3dSegments()
    {
        QVERIFY(!parse3dSegment(QByteArray("(3001,10,x)E")).hasValue());
        QVERIFY(!parse3dSegment(QByteArray("(1001)E")).hasValue());
        QVERIFY(!parse3dSegment(QByteArray("(abc,10,x)E")).hasValue());
    }
};

QObject* createCameraProtocolTest()
{
    return new CameraProtocolTest();
}

#include "test_camera_protocol.moc"
