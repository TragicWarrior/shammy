#pragma once

#include "mcp/McpHost.h"
#include "openai/ChatTypes.h"

#include <QAbstractListModel>

class BackendListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, BaseUrlRole, ApiKeyRole, EnabledRole };
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
        case EnabledRole:
            return b.enabled;
        default:
            return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{IdRole, "backendId"},
                {NameRole, "name"},
                {BaseUrlRole, "baseUrl"},
                {ApiKeyRole, "apiKey"},
                {EnabledRole, "enabled"}};
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
    struct Entry
    {
        QString backendId;
        QString backendName;
        QString model;
        bool vision = false;
        bool tools = false;
        bool thinking = false;
        bool audio = false;
        bool advertised = false;
        bool overridden = false;
        int context = 16384;
        QString contextLabel;
    };
    enum Roles
    {
        NameRole = Qt::UserRole + 1,
        BackendIdRole,
        BackendNameRole,
        LabelRole,
        CapVisionRole,
        CapToolsRole,
        CapThinkingRole,
        CapAudioRole,
        CapAdvertisedRole,
        CapOverriddenRole,
        ContextRole,
        ContextLabelRole
    };
    explicit ModelListModel(QObject *parent = nullptr)
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
        {
            return {};
        }
        const Entry &e = m_items.at(index.row());
        switch (role)
        {
        case NameRole:
        case Qt::DisplayRole:
            return e.model;
        case BackendIdRole:
            return e.backendId;
        case BackendNameRole:
            return e.backendName;
        case LabelRole:
            return e.backendName.isEmpty() ? e.model
                                           : (e.backendName + QStringLiteral(" / ") + e.model);
        case CapVisionRole:
            return e.vision;
        case CapToolsRole:
            return e.tools;
        case CapThinkingRole:
            return e.thinking;
        case CapAudioRole:
            return e.audio;
        case CapAdvertisedRole:
            return e.advertised;
        case CapOverriddenRole:
            return e.overridden;
        case ContextRole:
            return e.context;
        case ContextLabelRole:
            return e.contextLabel;
        default:
            return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{NameRole, "name"},
                {BackendIdRole, "backendId"},
                {BackendNameRole, "backendName"},
                {LabelRole, "label"},
                {CapVisionRole, "capVision"},
                {CapToolsRole, "capTools"},
                {CapThinkingRole, "capThinking"},
                {CapAudioRole, "capAudio"},
                {CapAdvertisedRole, "capAdvertised"},
                {CapOverriddenRole, "capOverridden"},
                {ContextRole, "context"},
                {ContextLabelRole, "contextLabel"}};
    }
    void updateContext(int row, int context, const QString &label)
    {
        if (row < 0 || row >= m_items.size())
        {
            return;
        }
        m_items[row].context = context;
        m_items[row].contextLabel = label;
        const QModelIndex ix = index(row);
        emit dataChanged(ix, ix, {ContextRole, ContextLabelRole});
    }
    // Refresh the capability fields of one row in place (used when a probe
    // completes or the user overrides a capability).
    void updateCaps(int row, bool vision, bool tools, bool thinking, bool audio, bool advertised,
                    bool overridden)
    {
        if (row < 0 || row >= m_items.size())
        {
            return;
        }
        Entry &e = m_items[row];
        e.vision = vision;
        e.tools = tools;
        e.thinking = thinking;
        e.audio = audio;
        e.advertised = advertised;
        e.overridden = overridden;
        const QModelIndex ix = index(row);
        emit dataChanged(ix, ix,
                         {CapVisionRole, CapToolsRole, CapThinkingRole, CapAudioRole,
                          CapAdvertisedRole, CapOverriddenRole});
    }
    void setEntries(const QList<Entry> &items)
    {
        beginResetModel();
        m_items = items;
        endResetModel();
    }
    // Model names across all backends, in list order (may contain duplicates
    // when the same model name is served by more than one backend).
    QStringList ids() const
    {
        QStringList out;
        out.reserve(m_items.size());
        for (const Entry &e : m_items)
        {
            out.append(e.model);
        }
        return out;
    }
    int indexOf(const QString &backendId, const QString &model) const
    {
        for (int i = 0; i < m_items.size(); ++i)
        {
            if (m_items.at(i).backendId == backendId && m_items.at(i).model == model)
            {
                return i;
            }
        }
        return -1;
    }
    Entry at(int row) const
    {
        return (row >= 0 && row < m_items.size()) ? m_items.at(row) : Entry{};
    }

private:
    QList<Entry> m_items;
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
