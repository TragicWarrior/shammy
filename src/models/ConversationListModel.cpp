#include "models/ConversationListModel.h"

ConversationListModel::ConversationListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ConversationListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QHash<int, QByteArray> ConversationListModel::roleNames() const
{
    return
    {
        {IdRole, "conversationId"},
        {TitleRole, "title"},
        {ProjectIdRole, "projectId"},
        {ModelRole, "modelName"},
        {PinnedRole, "pinned"},
        {UpdatedAtRole, "updatedAt"},
        {BackendIdRole, "backendId"},
    };
}

QVariant ConversationListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Conversation &c = m_items.at(index.row());
    switch (role)
    {
    case IdRole:
        return c.id;
    case TitleRole:
        return c.title;
    case ProjectIdRole:
        return c.projectId;
    case ModelRole:
        return c.model;
    case PinnedRole:
        return c.pinned;
    case UpdatedAtRole:
        return c.updatedAt;
    case BackendIdRole:
        return c.backendId;
    default:
        return {};
    }
}

void ConversationListModel::setItems(const QList<Conversation> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}
