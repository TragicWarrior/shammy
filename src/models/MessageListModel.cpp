#include "models/MessageListModel.h"
#include "artifacts/ContentSplitter.h"

MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return
    {
        {IdRole, "messageId"},
        {RoleNameRole, "role"},
        {ContentRole, "content"},
        {ReasoningRole, "reasoning"},
        {StreamingRole, "streaming"},
        {PartsRole, "parts"},
        {ToolCallsRole, "toolCalls"},
        {ToolCallIdRole, "toolCallId"},
        {ErrorRole, "error"},
    };
}

static QVariantList splitParts(const QString &content)
{
    QVariantList list;
    const auto parts = ContentSplitter::split(content);
    list.reserve(parts.size());
    for (const ContentPart &c : parts)
    {
        QVariantMap p;
        p.insert(QStringLiteral("type"), c.type);
        p.insert(QStringLiteral("text"), c.text);
        p.insert(QStringLiteral("language"), c.language);
        p.insert(QStringLiteral("identifier"), c.identifier);
        p.insert(QStringLiteral("title"), c.title);
        p.insert(QStringLiteral("mime"), c.mime);
        list.append(p);
    }
    return list;
}

QVariantList MessageListModel::partsOf(const ChatMessage &m) const
{
    if (m.streaming)
        return {};
    if (!m.partsReady)
    {
        m.cachedParts = splitParts(m.content);
        m.partsReady = true;
    }
    return m.cachedParts;
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const ChatMessage &m = m_items.at(index.row());
    switch (role)
    {
    case IdRole:
        return m.id;
    case RoleNameRole:
        return m.role;
    case ContentRole:
        return m.content;
    case ReasoningRole:
        return m.reasoning;
    case StreamingRole:
        return m.streaming;
    case PartsRole:
        return partsOf(m);
    case ToolCallsRole:
        return m.toolCallsJson;
    case ToolCallIdRole:
        return m.toolCallId;
    case ErrorRole:
        return m.error;
    default:
        return {};
    }
}

void MessageListModel::setMessages(const QVector<ChatMessage> &msgs)
{
    beginResetModel();
    m_items = msgs;
    endResetModel();
}

void MessageListModel::append(const ChatMessage &m)
{
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append(m);
    endInsertRows();
}

void MessageListModel::appendAssistantDelta(const QString &text)
{
    if (m_items.isEmpty())
        return;
    m_items.last().content += text;
    m_items.last().partsReady = false;
    m_items.last().cachedParts.clear();
    const QModelIndex ix = index(m_items.size() - 1);
    emit dataChanged(ix, ix, {ContentRole});
}

void MessageListModel::appendReasoningDelta(const QString &text)
{
    if (m_items.isEmpty())
        return;
    m_items.last().reasoning += text;
    const QModelIndex ix = index(m_items.size() - 1);
    emit dataChanged(ix, ix, {ReasoningRole});
}

void MessageListModel::finishLast()
{
    if (m_items.isEmpty())
    {
        return;
    }
    m_items.last().streaming = false;
    const QModelIndex ix = index(m_items.size() - 1);
    emit dataChanged(ix, ix,
                     {StreamingRole, PartsRole, ContentRole, ReasoningRole, ToolCallsRole, ErrorRole});
}

void MessageListModel::setLastError(const QString &err)
{
    if (m_items.isEmpty())
        return;
    m_items.last().error = err;
    m_items.last().streaming = false;
    const QModelIndex ix = index(m_items.size() - 1);
    emit dataChanged(ix, ix, {ErrorRole, StreamingRole});
}

void MessageListModel::setLastToolCalls(const QString &json)
{
    if (m_items.isEmpty())
        return;
    m_items.last().toolCallsJson = json;
    const QModelIndex ix = index(m_items.size() - 1);
    emit dataChanged(ix, ix, {ToolCallsRole});
}

const ChatMessage &MessageListModel::last() const
{
    static const ChatMessage empty;
    return m_items.isEmpty() ? empty : m_items.last();
}

void MessageListModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}

QString MessageListModel::lastId() const
{
    return m_items.isEmpty() ? QString() : m_items.last().id;
}
