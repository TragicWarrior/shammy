#pragma once

#include "artifacts/FileImport.h"
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
    Q_PROPERTY(QString fileBudgetNote READ fileBudgetNote NOTIFY filesChanged)
    // Why a file could not be added (one line per file); empty when all went well.
    Q_PROPERTY(QString filesError READ filesError NOTIFY filesErrorChanged)
    // True while Word/spreadsheet files are being converted in the background.
    Q_PROPERTY(bool filesBusy READ filesBusy NOTIFY filesBusyChanged)
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
    QString fileBudgetNote() const;
    QString filesError() const { return m_filesError; }
    bool filesBusy() const { return m_busy > 0; }

    // Project instructions and files are sent with every chat, so they may use
    // only a share of the model's context window (kContextSharePercent), never
    // more than kMaxBudgetBytes. Sizes are bytes; a token is taken as ~4 bytes.
    static qint64 preloadBudgetBytes(int contextTokens);
    // No single file may take more than this much of that budget.
    static qint64 perFileLimitBytes(qint64 budgetBytes);
    // The context window (tokens) of the model selected in the app; the usage
    // gauge and the import warning are measured against it.
    void setContextTokens(int tokens);
    int contextTokens() const { return m_contextTokens; }
    qint64 preloadBudgetBytes() const { return preloadBudgetBytes(m_contextTokens); }

    Q_INVOKABLE void createProject(const QString &name, const QString &description = {});
    Q_INVOKABLE QString ensureImportedProject(const QString &sourceId, const QString &name,
                                             const QString &description);
    Q_INVOKABLE void renameProject(const QString &id, const QString &name);
    Q_INVOKABLE void deleteProject(const QString &id);
    Q_INVOKABLE void addFile(const QString &urlOrPath);
    Q_INVOKABLE void clearFilesError();
    Q_INVOKABLE void addFileFromContent(const QString &filename, const QByteArray &data);
    Q_INVOKABLE void removeFile(const QString &fileId);
    // The project's files as they are pasted into the system prompt, sized to
    // `contextTokens` (the window of the model that will read them). included /
    // truncated / omitted count files sent, cut short, and left out entirely.
    QString projectContextFor(const QString &projectId, int contextTokens, int *included, int *truncated,
                              int *omitted = nullptr) const;
    // Where a project's files live on disk.
    static QString projectsRoot();
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
    void filesErrorChanged();
    void filesBusyChanged();

private:
    void reload();
    void reloadFiles();
    QString projectDir(const QString &projectId) const;
    bool storeFile(const QString &projectId, const QString &filename, const QByteArray &data);
    bool storeCopy(const QString &projectId, const QString &sourcePath);
    void registerFile(const QString &projectId, const QString &filename, const QString &dest);
    void startConversion(const QString &projectId, const QString &path);
    void finishConversion(const QString &projectId, const FileImport::Result &result);
    void appendFilesError(const QString &message);
    // Only ever deletes inside projectsRoot(): a stored path is data from the
    // database, and must not be able to point this at anything else.
    void removeStoredFile(const QString &path);
    void removeProjectDir(const QString &projectId);
    Store *m_store = nullptr;
    ProjectListModel m_projects;
    QString m_currentId;
    QVariantList m_files;
    QString m_pane = QStringLiteral("chat");
    int m_contextTokens = 16384;
    QString m_filesError;
    int m_busy = 0;
};
