#pragma once

#include "openai/ChatTypes.h"

#include <QAbstractListModel>

class ConversationListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ProjectIdRole,
        ModelRole,
        PinnedRole,
        UpdatedAtRole,
        BackendIdRole
    };
    explicit ConversationListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setItems(const QList<Conversation> &items);

private:
    QList<Conversation> m_items;
};
