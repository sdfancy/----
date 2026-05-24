#pragma once

#include <QAbstractTableModel>
#include <QStringList>
#include <QDateTime>

namespace spray::ui {

struct EventLogItem {
    QDateTime timestamp;
    QString level;
    QString message;
    QString source;
};

class EventLogTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit EventLogTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addLog(const EventLogItem& item);

private:
    QList<EventLogItem> m_logs;
    QStringList m_headers;
};

} // namespace spray::ui
