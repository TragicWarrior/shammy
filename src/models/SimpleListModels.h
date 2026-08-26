#pragma once

#include "mcp/McpHost.h"
#include "openai/ChatTypes.h"

#include <QAbstractListModel>

class BackendListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, BaseUrlRole, ApiKeyRole };
    explicit BackendListModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_items.size();
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_items.size())
            return {};
        const Backend &b = m_items.at(index.row());
        switch (role)
        {
        case IdRole:
            return b.id;
        case NameRole:
            return b.name;
        case BaseUrlRole:
            return b.baseUrl;
        case ApiKeyRole:
            return b.apiKey;
        default:
            return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{IdRole, "backendId"},
                {NameRole, "name"},
                {BaseUrlRole, "baseUrl"},
                {ApiKeyRole, "apiKey"}};
    }
    void setItems(const QList<Backend> &items)
    {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

private:
    QList<Backend> m_items;
};

class ModelListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { NameRole = Qt::UserRole + 1 };
    explicit ModelListModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_ids.size();
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_ids.size())
            return {};
        if (role == NameRole || role == Qt::DisplayRole)
            return m_ids.at(index.row());
        return {};
    }
    QHash<int, QByteArray> roleNames() const override { return {{NameRole, "name"}}; }
    void setIds(const QStringList &ids)
    {
        beginResetModel();
        m_ids = ids;
        endResetModel();
    }
    QStringList ids() const { return m_ids; }

private:
    QStringList m_ids;
};

class ArtifactListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        IdentifierRole,
        TitleRole,
        TypeRole,
        LanguageRole,
        ContentRole,
        VersionRole,
        MessageIdRole
    };
    explicit ArtifactListModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_items.size();
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_items.size())
            return {};
        const Artifact &a = m_items.at(index.row());
        switch (role)
        {
        case IdRole:
            return a.id;
        case IdentifierRole:
            return a.identifier;
        case TitleRole:
        case Qt::DisplayRole:
            return a.title;
        case TypeRole:
            return a.type;
        case LanguageRole:
            return a.language;
        case ContentRole:
            return a.content;
        case VersionRole:
            return a.version;
        case MessageIdRole:
            return a.messageId;
        default:
            return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{IdRole, "artifactId"},     {IdentifierRole, "identifier"},
                {TitleRole, "title"},       {TypeRole, "type"},
                {LanguageRole, "language"}, {ContentRole, "content"},
                {VersionRole, "version"},   {MessageIdRole, "messageId"}};
    }
    void setItems(const QList<Artifact> &items)
    {
        beginResetModel();
        m_items = items;
        endResetModel();
    }
    Artifact at(int row) const
    {
        return (row >= 0 && row < m_items.size()) ? m_items.at(row) : Artifact{};
    }

private:
    QList<Artifact> m_items;
};

class McpServerListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        NameRole = Qt::UserRole + 1,
        CommandRole,
        EnabledRole,
        StateRole,
        ToolCountRole,
        ErrorRole
    };
    struct Row
    {
        QString name;
        QString command;
        bool enabled = true;
        QString state;
        int toolCount = 0;
        QString error;
    };
    explicit McpServerListModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_items.size();
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_items.size())
            return {};
        const Row &r = m_items.at(index.row());
        switch (role)
        {
        case NameRole:
            return r.name;
        case CommandRole:
            return r.command;
        case EnabledRole:
            return r.enabled;
        case StateRole:
            return r.state;
        case ToolCountRole:
            return r.toolCount;
        case ErrorRole:
            return r.error;
        default:
            return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{NameRole, "name"},
                {CommandRole, "command"},
                {EnabledRole, "enabled"},
                {StateRole, "state"},
                {ToolCountRole, "toolCount"},
                {ErrorRole, "error"}};
    }
    void setItems(const QList<Row> &items)
    {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

private:
    QList<Row> m_items;
};
