#pragma once

#include <QAbstractTableModel>
#include "domain/Snapshots.h"

namespace spray::ui {

class QueueTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit QueueTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void updateData(const spray::domain::QueueSnapshot& snapshot);

private:
    QList<spray::domain::QueueItemSnapshot> m_items;
    QStringList m_headers;
};

} // namespace spray::ui
