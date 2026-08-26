#pragma once

#include "openai/ChatTypes.h"

#include <QAbstractListModel>

class MessageListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        RoleNameRole,
        ContentRole,
        ReasoningRole,
        StreamingRole,
        PartsRole,
        ToolCallsRole,
        ToolCallIdRole,
        ErrorRole
    };

    explicit MessageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setMessages(const QVector<ChatMessage> &msgs);
    void append(const ChatMessage &m);
    void appendAssistantDelta(const QString &text);
    void appendReasoningDelta(const QString &text);
    void finishLast();
    void setLastError(const QString &err);
    void setLastToolCalls(const QString &json);
    const ChatMessage &last() const;
    const QVector<ChatMessage> &all() const { return m_items; }
    void clear();
    QString lastId() const;

private:
    QVariantList partsOf(const ChatMessage &m) const;
    QVector<ChatMessage> m_items;
};
