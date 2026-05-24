#include "EventLogTableModel.h"
#include <QBrush>
#include <QColor>

namespace spray::ui {

EventLogTableModel::EventLogTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    m_headers << "Time" << "Level" << "Message" << "Source";
    
    // Add some mock data
    addLog({QDateTime::currentDateTime().addSecs(-120), "INFO", "System initialized", "Core"});
    addLog({QDateTime::currentDateTime().addSecs(-60), "INFO", "PLC connected", "PlcEndpoint"});
    addLog({QDateTime::currentDateTime().addSecs(-10), "WARN", "Camera delay > 100ms", "CameraEndpoint"});
}

int EventLogTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_logs.count();
}

int EventLogTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_headers.count();
}

QVariant EventLogTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_logs.count()) return {};

    const auto& log = m_logs.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return log.timestamp.toString("HH:mm:ss.zzz");
            case 1: return log.level;
            case 2: return log.message;
            case 3: return log.source;
            default: return {};
        }
    } else if (role == Qt::ForegroundRole) {
        if (log.level == "ERROR") return QBrush(QColor("#f7768e"));
        if (log.level == "WARN") return QBrush(QColor("#e0af68"));
        if (log.level == "INFO") return QBrush(QColor("#a9b1d6"));
    }

    return {};
}

QVariant EventLogTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < m_headers.count()) {
            return m_headers.at(section);
        }
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

void EventLogTableModel::addLog(const EventLogItem& item)
{
    beginInsertRows(QModelIndex(), 0, 0);
    m_logs.prepend(item);
    if (m_logs.size() > 100) {
        m_logs.removeLast();
    }
    endInsertRows();
}

} // namespace spray::ui
