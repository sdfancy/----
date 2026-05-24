#include "QueueTableModel.h"
#include <QBrush>
#include <QColor>

namespace spray::ui {

QueueTableModel::QueueTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    m_headers << "Arm ID" << "Count" << "Pointer" << "Status" << "Source";
}

int QueueTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_items.count();
}

int QueueTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_headers.count();
}

QVariant QueueTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.count()) return {};

    const auto& item = m_items.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return item.armId;
            case 1: return item.count;
            case 2: return QString("0x%1").arg(item.pointer, 4, 16, QLatin1Char('0')).toUpper();
            case 3: return item.status;
            case 4: return item.source;
            default: return {};
        }
    } else if (role == Qt::ForegroundRole) {
        if (index.column() == 3) {
            if (item.status == "Done") return QBrush(QColor("#2ECC71"));
            if (item.status == "Pending") return QBrush(QColor("#3498DB"));
            if (item.status == "Ready") return QBrush(QColor("#F1C40F"));
        }
    } else if (role == Qt::TextAlignmentRole) {
        return Qt::AlignCenter;
    }

    return {};
}

QVariant QueueTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < m_headers.count()) {
            return m_headers.at(section);
        }
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

void QueueTableModel::updateData(const spray::domain::QueueSnapshot& snapshot)
{
    beginResetModel();
    m_items = snapshot.items;
    endResetModel();
}

} // namespace spray::ui
