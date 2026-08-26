#pragma once

#include "openai/ChatTypes.h"

#include <QAbstractListModel>

class ProjectListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        InstructionsRole,
        DefaultModelRole,
        UpdatedAtRole,
        ConversationCountRole,
        FileCountRole
    };
    explicit ProjectListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setItems(const QList<Project> &items);

private:
    QList<Project> m_items;
};
