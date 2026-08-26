#pragma once

#include "models/ProjectListModel.h"
#include "persist/Store.h"

#include <QByteArray>
#include <QObject>
#include <QVariantList>

class ProjectController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ProjectListModel *projects READ projects CONSTANT)
    Q_PROPERTY(QString currentProjectId READ currentProjectId WRITE setCurrentProjectId NOTIFY currentProjectIdChanged)
    Q_PROPERTY(QString currentProjectName READ currentProjectName NOTIFY currentProjectIdChanged)
    Q_PROPERTY(QString currentProjectDescription READ currentProjectDescription WRITE setCurrentProjectDescription NOTIFY currentProjectIdChanged)
    Q_PROPERTY(QString instructions READ instructions WRITE setInstructions NOTIFY currentProjectIdChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QString pane READ pane WRITE setPane NOTIFY paneChanged)
    Q_PROPERTY(int fileUsagePercent READ fileUsagePercent NOTIFY filesChanged)
    Q_PROPERTY(QString fileUsageLabel READ fileUsageLabel NOTIFY filesChanged)
    Q_PROPERTY(bool fileOverCapacity READ fileOverCapacity NOTIFY filesChanged)
public:
    explicit ProjectController(Store *store, QObject *parent = nullptr);

    ProjectListModel *projects() { return &m_projects; }
    QString currentProjectId() const { return m_currentId; }
    void setCurrentProjectId(const QString &id);
    QString currentProjectName() const;
    QString currentProjectDescription() const;
    void setCurrentProjectDescription(const QString &s);
    QString instructions() const;
    void setInstructions(const QString &s);
    QString instructionsFor(const QString &projectId) const;
    QVariantList files() const { return m_files; }
    QString pane() const { return m_pane; }
    void setPane(const QString &p);
    int fileUsagePercent() const;
    QString fileUsageLabel() const;
    bool fileOverCapacity() const;

    Q_INVOKABLE void createProject(const QString &name, const QString &description = {});
    Q_INVOKABLE QString ensureImportedProject(const QString &sourceId, const QString &name,
                                             const QString &description);
    Q_INVOKABLE void renameProject(const QString &id, const QString &name);
    Q_INVOKABLE void deleteProject(const QString &id);
    Q_INVOKABLE void addFile(const QString &urlOrPath);
    Q_INVOKABLE void addFileFromContent(const QString &filename, const QByteArray &data);
    Q_INVOKABLE void removeFile(const QString &fileId);
    QString projectContext(int *included, int *truncated) const;
    QString projectContextFor(const QString &projectId, int *included, int *truncated) const;
    Q_INVOKABLE void clearCurrent();
    Q_INVOKABLE void openOverview();
    Q_INVOKABLE void openProject(const QString &id);
    Q_INVOKABLE void showChat();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE QString formatUpdated(qint64 ms) const;

signals:
    void currentProjectIdChanged();
    void filesChanged();
    void paneChanged();

private:
    void reload();
    void reloadFiles();
    Store *m_store = nullptr;
    ProjectListModel m_projects;
    QString m_currentId;
    QVariantList m_files;
    QString m_pane = QStringLiteral("chat");
};
