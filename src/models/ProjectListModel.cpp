#include "models/ProjectListModel.h"

ProjectListModel::ProjectListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ProjectListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QHash<int, QByteArray> ProjectListModel::roleNames() const
{
    return
    {
        {IdRole, "projectId"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {InstructionsRole, "instructions"},
        {DefaultModelRole, "defaultModel"},
        {UpdatedAtRole, "updatedAt"},
        {ConversationCountRole, "conversationCount"},
        {FileCountRole, "fileCount"},
    };
}

QVariant ProjectListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Project &p = m_items.at(index.row());
    switch (role)
    {
    case IdRole:
        return p.id;
    case NameRole:
        return p.name;
    case DescriptionRole:
        return p.description;
    case InstructionsRole:
        return p.instructions;
    case DefaultModelRole:
        return p.defaultModel;
    case UpdatedAtRole:
        return p.updatedAt;
    case ConversationCountRole:
        return p.conversationCount;
    case FileCountRole:
        return p.fileCount;
    default:
        return {};
    }
}

void ProjectListModel::setItems(const QList<Project> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}
