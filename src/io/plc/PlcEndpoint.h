#pragma once

#include "config/AppConfig.h"

#include <QByteArray>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace spray::io {

class PlcEndpoint final : public QObject {
    Q_OBJECT

public:
    explicit PlcEndpoint(config::PlcConfig config, QObject* parent = nullptr);

    bool start(QString* errorMessage = nullptr);
    void stop();
    bool sendEnqueueFeedback(const QByteArray& bytes);
    bool sendDequeueFeedback(const QByteArray& bytes);
    int enqueueConnectionCount() const;
    int dequeueConnectionCount() const;

signals:
    void enqueueFrameReceived(QByteArray bytes);
    void dequeueFrameReceived(QByteArray bytes);
    void rawFrame(QString channel, QString direction, QByteArray bytes);
    void warning(QString message);

private:
    void wireServer(QTcpServer& server, QList<QTcpSocket*>& sockets, const QString& channel);
    void removeSocket(QList<QTcpSocket*>& sockets, QTcpSocket* socket);
    bool sendFeedback(QList<QTcpSocket*>& sockets,
                      const QString& channel,
                      const QByteArray& bytes,
                      const QString& missingConnectionMessage);

    config::PlcConfig config_;
    QTcpServer enqueueServer_;
    QTcpServer dequeueServer_;
    QList<QTcpSocket*> enqueueSockets_;
    QList<QTcpSocket*> dequeueSockets_;
};

} // namespace spray::io
