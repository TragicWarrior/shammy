#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>

#include <functional>

class ProjectController;
class SettingsController;
class Store;

class ClaudeImporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString importingId READ importingId NOTIFY importingIdChanged)
public:
    explicit ClaudeImporter(ProjectController *projects, Store *store, SettingsController *settings,
                            QObject *parent = nullptr);

    QVariantList projects() const { return m_projects; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString status() const { return m_status; }
    QString importingId() const { return m_importingId; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void importProject(const QString &uuid);

signals:
    void projectsChanged();
    void busyChanged();
    void errorChanged();
    void statusChanged();
    void importingIdChanged();

private:
    struct Session
    {
        QString orgId;
        QString cookieHeader;
    };

    bool ensureSession(QString *error);
    void setBusy(bool v);
    void setError(const QString &s);
    void setStatus(const QString &s);
    void setImportingId(const QString &id);
    void getJson(const QString &path, const std::function<void(QJsonDocument, QString)> &done);
    void importNextChat();
    void finishImport();

    ProjectController *m_projectsCtl = nullptr;
    Store *m_store = nullptr;
    SettingsController *m_settings = nullptr;
    QNetworkAccessManager m_nam;
    Session m_session;
    QVariantList m_projects;
    bool m_busy = false;
    int m_pending = 0;
    QString m_error;
    QString m_status;
    QString m_importingId;
    QString m_importLocalId;
    QString m_importName;
    int m_importFiles = 0;
    int m_importChats = 0;
    int m_importChatTotal = 0;
    bool m_importOverCap = false;
    bool m_importUpdated = false;
    QList<QJsonObject> m_chatQueue;
};
