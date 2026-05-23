#include "test_plc_protocol.h"

#include "domain/Types.h"
#include "protocol/PlcProtocol.h"

#include <QtTest/QtTest>

using spray::domain::FeedbackStage;
using spray::protocol::parseDequeueFrame;
using spray::protocol::parseEnqueueFrame;

class PlcProtocolTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesEnqueueFrame()
    {
        const auto result = parseEnqueueFrame(QByteArray::fromHex("0001000A0001"));

        QVERIFY(result.hasValue());
        QCOMPARE(result.value->command, 1);
        QCOMPARE(result.value->count, 10);
        QCOMPARE(result.value->pointer, 1);
        QCOMPARE(result.value->extra, QByteArray());
    }

    void keepsEnqueueExtraBytes()
    {
        const auto result = parseEnqueueFrame(QByteArray::fromHex("0001000A0001AABB"));

        QVERIFY(result.hasValue());
        QCOMPARE(result.value->extra, QByteArray::fromHex("AABB"));
    }

    void rejectsShortEnqueueFrame()
    {
        const auto result = parseEnqueueFrame(QByteArray::fromHex("0001000A"));

        QVERIFY(!result.hasValue());
        QVERIFY(result.error.has_value());
    }

    void parsesDequeueFrame()
    {
        const auto result = parseDequeueFrame(QByteArray::fromHex("00010000"));

        QVERIFY(result.hasValue());
        QCOMPARE(result.value->arm1Pointer, 1);
        QCOMPARE(result.value->arm2Pointer, 0);
    }

    void rejectsInvalidDequeueLength()
    {
        const auto result = parseDequeueFrame(QByteArray::fromHex("000100"));

        QVERIFY(!result.hasValue());
        QVERIFY(result.error.has_value());
    }

    void buildsFeedbackCodes()
    {
        QCOMPARE(spray::protocol::buildFeedback(1, FeedbackStage::Normal), QByteArray("1N"));
        QCOMPARE(spray::protocol::buildFeedback(1, FeedbackStage::Done), QByteArray("1D"));
        QCOMPARE(spray::protocol::buildFeedback(2, FeedbackStage::Normal), QByteArray("2N"));
        QCOMPARE(spray::protocol::buildFeedback(2, FeedbackStage::Done), QByteArray("2D"));
    }
};

QObject* createPlcProtocolTest()
{
    return new PlcProtocolTest();
}

#include "test_plc_protocol.moc"
