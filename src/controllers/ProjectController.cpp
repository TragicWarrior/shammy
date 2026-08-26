#include "controllers/ProjectController.h"
#include "Util.h"
#include "artifacts/SpreadsheetExtract.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

static constexpr qint64 kPerFileLimit = 32 * 1024;
static constexpr qint64 kTotalLimit = 256 * 1024;

ProjectController::ProjectController(Store *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    connect(m_store, &Store::projectsChanged, this, &ProjectController::reload);
    connect(m_store, &Store::conversationsChanged, this, &ProjectController::reload);
    reload();
}

void ProjectController::reload()
{
    QList<Project> items = m_store->projects();
    for (Project &p : items)
    {
        p.conversationCount = m_store->conversations(p.id).size();
        const auto files = m_store->projectFiles(p.id);
        p.fileCount = files.size();
        p.filesBytes = 0;
        for (const ProjectFile &f : files)
        {
            p.filesBytes += f.size;
        }
    }
    m_projects.setItems(items);
    reloadFiles();
}

void ProjectController::reloadFiles()
{
    m_files.clear();
    if (!m_currentId.isEmpty())
    {
        for (const ProjectFile &f : m_store->projectFiles(m_currentId))
        {
            m_files.append(QVariantMap
            {
                {QStringLiteral("id"), f.id},
                {QStringLiteral("filename"), f.filename},
                {QStringLiteral("size"), f.size},
                {QStringLiteral("path"), f.path},
            });
        }
    }
    emit filesChanged();
}

void ProjectController::setCurrentProjectId(const QString &id)
{
    if (m_currentId == id)
        return;
    m_currentId = id;
    reloadFiles();
    emit currentProjectIdChanged();
}

void ProjectController::clearCurrent()
{
    setCurrentProjectId({});
}

QString ProjectController::currentProjectName() const
{
    return m_store->project(m_currentId).name;
}

QString ProjectController::currentProjectDescription() const
{
    return m_store->project(m_currentId).description;
}

void ProjectController::setCurrentProjectDescription(const QString &s)
{
    Project p = m_store->project(m_currentId);
    if (p.id.isEmpty())
    {
        return;
    }
    if (p.description == s)
    {
        return;
    }
    p.description = s;
    p.updatedAt = nowMs();
    m_store->upsertProject(p);
    emit currentProjectIdChanged();
}

QString ProjectController::instructions() const
{
    return instructionsFor(m_currentId);
}

QString ProjectController::instructionsFor(const QString &projectId) const
{
    if (projectId.isEmpty())
    {
        return {};
    }
    return m_store->project(projectId).instructions;
}

void ProjectController::setInstructions(const QString &s)
{
    Project p = m_store->project(m_currentId);
    if (p.id.isEmpty())
        return;
    p.instructions = s;
    p.updatedAt = nowMs();
    m_store->upsertProject(p);
    emit currentProjectIdChanged();
    emit filesChanged();
}

void ProjectController::createProject(const QString &name, const QString &description)
{
    Project p;
    p.id = newId();
    p.name = name.trimmed().isEmpty() ? QStringLiteral("Untitled project") : name.trimmed();
    p.description = description.trimmed();
    p.createdAt = p.updatedAt = nowMs();
    m_store->upsertProject(p);
    openProject(p.id);
}

QString ProjectController::ensureImportedProject(const QString &sourceId, const QString &name,
                                                 const QString &description)
{
    Project p = m_store->projectBySourceId(sourceId);
    if (p.id.isEmpty())
        p = m_store->projectByName(name);
    if (p.id.isEmpty())
    {
        p.id = newId();
        p.createdAt = nowMs();
    }
    p.name = name.trimmed().isEmpty() ? QStringLiteral("Untitled project") : name.trimmed();
    p.description = description.trimmed();
    p.sourceId = sourceId;
    p.updatedAt = nowMs();
    m_store->upsertProject(p);
    openProject(p.id);
    return p.id;
}

void ProjectController::setPane(const QString &p)
{
    const QString next = p.isEmpty() ? QStringLiteral("chat") : p;
    if (m_pane == next)
    {
        return;
    }
    m_pane = next;
    emit paneChanged();
}

void ProjectController::openOverview()
{
    setCurrentProjectId({});
    setPane(QStringLiteral("overview"));
}

void ProjectController::openProject(const QString &id)
{
    setCurrentProjectId(id);
    setPane(QStringLiteral("project"));
}

void ProjectController::showChat()
{
    setPane(QStringLiteral("chat"));
}

void ProjectController::goHome()
{
    setCurrentProjectId({});
    setPane(QStringLiteral("chat"));
}

static qint64 preloadBytes(Store *store, const QString &projectId)
{
    qint64 bytes = store->project(projectId).instructions.toUtf8().size();
    for (const ProjectFile &f : store->projectFiles(projectId))
    {
        bytes += f.size;
    }
    return bytes;
}

int ProjectController::fileUsagePercent() const
{
    const qint64 bytes = preloadBytes(m_store, m_currentId);
    return int((bytes * 100) / kTotalLimit);
}

bool ProjectController::fileOverCapacity() const
{
    return preloadBytes(m_store, m_currentId) > kTotalLimit;
}

QString ProjectController::fileUsageLabel() const
{
    const qint64 bytes = preloadBytes(m_store, m_currentId);
    auto fmt = [](qint64 n) -> QString
    {
        if (n < 1024)
        {
            return QString::number(n) + QStringLiteral(" B");
        }
        return QString::number(double(n) / 1024.0, 'f', 1) + QStringLiteral(" KB");
    };
    QString s = fmt(bytes) + QStringLiteral(" / ") + fmt(kTotalLimit);
    if (bytes > kTotalLimit)
        s += QStringLiteral("  ·  over cap");
    return s;
}

QString ProjectController::formatUpdated(qint64 ms) const
{
    if (ms <= 0)
    {
        return {};
    }
    const qint64 now = nowMs();
    const qint64 mins = qMax(qint64(0), (now - ms) / 60000);
    if (mins < 1)
    {
        return QStringLiteral("Just now");
    }
    if (mins < 60)
    {
        return QStringLiteral("%1 min ago").arg(mins);
    }
    const qint64 hours = mins / 60;
    if (hours < 24)
    {
        return hours == 1 ? QStringLiteral("1 hour ago") : QStringLiteral("%1 hours ago").arg(hours);
    }
    const qint64 days = hours / 24;
    if (days == 1)
    {
        return QStringLiteral("Yesterday");
    }
    if (days < 7)
    {
        return QStringLiteral("%1 days ago").arg(days);
    }
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("MMM d"));
}

void ProjectController::renameProject(const QString &id, const QString &name)
{
    Project p = m_store->project(id);
    if (p.id.isEmpty())
        return;
    p.name = name;
    p.updatedAt = nowMs();
    m_store->upsertProject(p);
    if (id == m_currentId)
        emit currentProjectIdChanged();
}

void ProjectController::deleteProject(const QString &id)
{
    m_store->deleteProject(id);
    if (m_currentId == id)
        setCurrentProjectId({});
}

void ProjectController::addFile(const QString &urlOrPath)
{
    if (m_currentId.isEmpty())
        return;
    QString path = urlOrPath;
    const QUrl u(urlOrPath);
    if (u.isLocalFile())
        path = u.toLocalFile();
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
        return;
    if (SpreadsheetExtract::isSpreadsheetPath(path))
    {
        QString err;
        const QString office = QSettings().value(QStringLiteral("officeBinaryPath")).toString();
        const auto sheets = SpreadsheetExtract::extract(path, office, &err);
        if (!sheets.isEmpty())
        {
            const QString stem = fi.completeBaseName();
            for (const SpreadsheetExtract::Sheet &sh : sheets)
            {
                const QString fn = stem + QLatin1Char('-') + SpreadsheetExtract::safeSheetFileName(sh.name)
                    + QStringLiteral(".csv");
                addFileFromContent(fn, sh.csv.toUtf8());
            }
            return;
        }
    }
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return;
    addFileFromContent(fi.fileName(), in.readAll());
}

void ProjectController::addFileFromContent(const QString &filename, const QByteArray &data)
{
    if (m_currentId.isEmpty() || filename.trimmed().isEmpty())
        return;
    const QString safe = QFileInfo(filename).fileName();
    const QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/projects/") + m_currentId;
    QDir().mkpath(destDir);
    const QString dest = destDir + QLatin1Char('/') + safe;
    QFile::remove(dest);
    QFile out(dest);
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(data);
    out.close();
    ProjectFile f;
    f.id = newId();
    for (const ProjectFile &existing : m_store->projectFiles(m_currentId))
    {
        if (existing.filename == safe)
        {
            f.id = existing.id;
            break;
        }
    }
    f.projectId = m_currentId;
    f.filename = safe;
    f.mime = QMimeDatabase().mimeTypeForFile(dest).name();
    f.path = dest;
    f.size = data.size();
    m_store->upsertProjectFile(f);
    Project p = m_store->project(m_currentId);
    p.updatedAt = nowMs();
    m_store->upsertProject(p);
    reloadFiles();
}

void ProjectController::removeFile(const QString &fileId)
{
    m_store->deleteProjectFile(fileId);
    reloadFiles();
}

QString ProjectController::projectContext(int *included, int *truncated) const
{
    return projectContextFor(m_currentId, included, truncated);
}

QString ProjectController::projectContextFor(const QString &projectId, int *included, int *truncated) const
{
    if (included)
        *included = 0;
    if (truncated)
        *truncated = 0;
    if (projectId.isEmpty())
        return {};
    QString out;
    qint64 total = 0;
    for (const ProjectFile &f : m_store->projectFiles(projectId))
    {
        if (SpreadsheetExtract::isSpreadsheetPath(f.path))
        {
            out += QStringLiteral("\n## %1\n(spreadsheet not extracted — install LibreOffice or convert to CSV)\n")
                       .arg(f.filename);
            if (included)
                *included += 1;
            continue;
        }
        QFile file(f.path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QByteArray data = file.read(kPerFileLimit + 1);
        bool trunc = data.size() > kPerFileLimit;
        if (trunc)
            data.truncate(kPerFileLimit);
        if (total + data.size() > kTotalLimit)
        {
            data.truncate(int(kTotalLimit - total));
            trunc = true;
        }
        total += data.size();
        out += QStringLiteral("\n## %1\n```\n").arg(f.filename);
        out += QString::fromUtf8(data);
        out += QStringLiteral("\n```\n");
        if (included)
            *included += 1;
        if (trunc && truncated)
            *truncated += 1;
        if (total >= kTotalLimit)
            break;
    }
    return out;
}
