#pragma once

#include "openai/ChatTypes.h"

#include <QObject>
#include <QSqlDatabase>
#include <QStringList>

class Store : public QObject
{
    Q_OBJECT
public:
    explicit Store(QObject *parent = nullptr);
    ~Store() override;

    bool open(const QString &dbPath);
    QString lastError() const { return m_error; }
    bool hasFts() const { return m_fts; }
    QString dbPath() const { return m_path; }

    QString setting(const QString &key, const QString &def = {}) const;
    void setSetting(const QString &key, const QString &value);

    QList<Backend> backends() const;
    void upsertBackend(const Backend &b);
    void deleteBackend(const QString &id);
    Backend backend(const QString &id) const;

    QList<Project> projects() const;
    Project project(const QString &id) const;
    Project projectBySourceId(const QString &sourceId) const;
    Project projectByName(const QString &name) const;
    void upsertProject(const Project &p);
    void deleteProject(const QString &id);
    QList<ProjectFile> projectFiles(const QString &projectId) const;
    void upsertProjectFile(const ProjectFile &f);
    void deleteProjectFile(const QString &id);

    QList<Conversation> conversations(const QString &projectId = {}) const;
    Conversation conversation(const QString &id) const;
    void upsertConversation(const Conversation &c);
    void deleteConversation(const QString &id);
    QList<Conversation> search(const QString &query) const;

    QList<Message> messages(const QString &conversationId) const;
    void upsertMessage(const Message &m);
    void deleteMessage(const QString &id);
    void deleteMessages(const QStringList &ids);
    void deleteMessagesFrom(const QString &conversationId, qint64 fromCreatedAt);
    void deleteMessagesForConversation(const QString &conversationId);

    QList<Artifact> artifactsForConversation(const QString &conversationId) const;
    QList<Artifact> artifactsForIdentifier(const QString &conversationId, const QString &identifier) const;
    int nextArtifactVersion(const QString &conversationId, const QString &identifier) const;
    void insertArtifact(const Artifact &a);
    void deleteArtifactsForConversation(const QString &conversationId);

    bool isAlwaysAllowed(const QString &server, const QString &tool) const;
    void allowAlways(const QString &server, const QString &tool);
    void revokeAlways(const QString &server, const QString &tool);

signals:
    void backendsChanged();
    void projectsChanged();
    void conversationsChanged();
    void messagesChanged(const QString &conversationId);
    void artifactsChanged(const QString &conversationId);

private:
    bool exec(const QString &sql);
    bool migrate();
    void indexMessage(const Message &m);
    void reindexConversation(const QString &conversationId);

    QSqlDatabase m_db;
    QString m_path;
    QString m_error;
    bool m_fts = false;
    QString m_connName;
};
