#include "controllers/ProjectController.h"
#include "Util.h"
#include "artifacts/Attach.h"
#include "artifacts/SpreadsheetExtract.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

// Project instructions and files ride along with every chat, so they may use a
// share of the model's context window and no more: a prompt that overflows the
// window is cut from the front by the server, which drops the system prompt.
static constexpr int kContextSharePercent = 40; // of the window, leaving room to talk
static constexpr int kBytesPerToken = 4;
static constexpr qint64 kMaxBudgetBytes = 256 * 1024;
static constexpr qint64 kMinBudgetBytes = 2 * 1024;
static constexpr qint64 kMinPerFileBytes = 32 * 1024;

qint64 ProjectController::preloadBudgetBytes(int contextTokens)
{
    const qint64 share = qint64(qMax(0, contextTokens)) * kBytesPerToken * kContextSharePercent / 100;
    return qBound(kMinBudgetBytes, share, kMaxBudgetBytes);
}

qint64 ProjectController::perFileLimitBytes(qint64 budgetBytes)
{
    return qMin(budgetBytes, qMax(kMinPerFileBytes, budgetBytes / 2));
}

static QString formatBytes(qint64 n)
{
    if (n < 1024)
        return QString::number(n) + QStringLiteral(" B");
    return QString::number(double(n) / 1024.0, 'f', 1) + QStringLiteral(" KB");
}

static QString formatTokens(int n)
{
    if (n >= 1024 * 1024 && n % (1024 * 1024) == 0)
        return QString::number(n / (1024 * 1024)) + QLatin1Char('M');
    if (n >= 1024)
        return QString::number(n / 1024) + QLatin1Char('K');
    return QString::number(n);
}

// Cutting UTF-8 at a byte count can leave half a character at the end.
static void trimToUtf8Boundary(QByteArray &b)
{
    int i = b.size();
    int continuation = 0;
    while (i > 0 && continuation < 4 && (static_cast<uchar>(b.at(i - 1)) & 0xC0) == 0x80)
    {
        --i;
        ++continuation;
    }
    if (i == 0)
        return;
    const uchar lead = static_cast<uchar>(b.at(i - 1));
    const int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (need > 1 && continuation + 1 < need)
        b.truncate(i - 1);
}

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
    clearFilesError();
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

// What a project sends with every chat, before any of it is cut to fit.
struct PreloadUsage
{
    qint64 bytes = 0;
    bool oneFileTooBig = false;
};

static PreloadUsage preloadUsage(Store *store, const QString &projectId, qint64 perFileLimit)
{
    PreloadUsage u;
    u.bytes = store->project(projectId).instructions.toUtf8().size();
    for (const ProjectFile &f : store->projectFiles(projectId))
    {
        u.bytes += f.size;
        if (f.size > perFileLimit)
            u.oneFileTooBig = true;
    }
    return u;
}

int ProjectController::fileUsagePercent() const
{
    const qint64 budget = preloadBudgetBytes();
    return int((preloadUsage(m_store, m_currentId, perFileLimitBytes(budget)).bytes * 100) / budget);
}

bool ProjectController::fileOverCapacity() const
{
    const qint64 budget = preloadBudgetBytes();
    const PreloadUsage u = preloadUsage(m_store, m_currentId, perFileLimitBytes(budget));
    return u.bytes > budget || u.oneFileTooBig;
}

QString ProjectController::fileUsageLabel() const
{
    const qint64 budget = preloadBudgetBytes();
    const PreloadUsage u = preloadUsage(m_store, m_currentId, perFileLimitBytes(budget));
    QString s = formatBytes(u.bytes) + QStringLiteral(" / ") + formatBytes(budget);
    if (u.bytes > budget || u.oneFileTooBig)
        s += QStringLiteral("  ·  over budget");
    return s;
}

QString ProjectController::fileBudgetNote() const
{
    return QStringLiteral("Each chat gets at most %1 of instructions and files: %2% of the selected model's %3-token "
                          "context, so there is room left to talk. Anything over is cut; everything stays saved here.")
        .arg(formatBytes(preloadBudgetBytes()))
        .arg(kContextSharePercent)
        .arg(formatTokens(m_contextTokens));
}

void ProjectController::setContextTokens(int tokens)
{
    if (tokens <= 0 || tokens == m_contextTokens)
        return;
    m_contextTokens = tokens;
    emit filesChanged(); // the usage figures depend on it
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
    const QString next = name.trimmed();
    if (next.isEmpty() || next == p.name)
        return;
    p.name = next;
    p.updatedAt = nowMs();
    m_store->upsertProject(p);
    if (id == m_currentId)
        emit currentProjectIdChanged();
}

void ProjectController::deleteProject(const QString &id)
{
    m_store->deleteProject(id);
    removeProjectDir(id);
    if (m_currentId == id)
        setCurrentProjectId({});
}

QString ProjectController::projectsRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/projects");
}

// A project id names a folder, so it must be a single plain path component.
static bool isPlainName(const QString &s)
{
    return !s.isEmpty() && s != QLatin1String(".") && s != QLatin1String("..") && !s.contains(QLatin1Char('/'))
        && !s.contains(QLatin1Char('\\'));
}

QString ProjectController::projectDir(const QString &projectId) const
{
    return isPlainName(projectId) ? projectsRoot() + QLatin1Char('/') + projectId : QString();
}

void ProjectController::appendFilesError(const QString &message)
{
    if (message.isEmpty())
        return;
    if (!m_filesError.isEmpty())
        m_filesError += QLatin1Char('\n');
    m_filesError += message;
    emit filesErrorChanged();
}

void ProjectController::clearFilesError()
{
    if (m_filesError.isEmpty())
        return;
    m_filesError.clear();
    emit filesErrorChanged();
}

void ProjectController::addFile(const QString &urlOrPath)
{
    if (m_currentId.isEmpty())
        return;
    QString path = urlOrPath;
    const QUrl u(urlOrPath);
    if (u.isLocalFile())
        path = u.toLocalFile();
    const QFileInfo fi(path);
    const QString name = fi.fileName().isEmpty() ? path : fi.fileName();
    if (!fi.exists())
    {
        appendFilesError(QStringLiteral("Can't add `%1`: file not found.").arg(name));
        return;
    }
    if (!fi.isFile())
    {
        appendFilesError(fi.isDir() ? QStringLiteral("Can't add `%1`: that's a folder.").arg(name)
                                    : QStringLiteral("Can't add `%1`: not a regular file.").arg(name));
        return;
    }
    // Project files are preloaded as text: decide now what this one can become.
    const FileImport::Classification c =
        FileImport::classify(path, QSettings().value(QStringLiteral("pdftotextBinaryPath")).toString());
    if (c.plan == FileImport::Plan::Refuse)
    {
        appendFilesError(c.refusal);
        return;
    }
    if (c.plan == FileImport::Plan::Copy)
    {
        if (!storeCopy(m_currentId, path))
            appendFilesError(QStringLiteral("Can't add `%1`: it could not be copied.").arg(name));
        return;
    }
    startConversion(m_currentId, path);
}

// Word and spreadsheet files go through LibreOffice, which can take a while:
// do it on a thread, and store the result when it comes back.
void ProjectController::startConversion(const QString &projectId, const QString &path)
{
    const QString office = QSettings().value(QStringLiteral("officeBinaryPath")).toString();
    const QString pdftotext = QSettings().value(QStringLiteral("pdftotextBinaryPath")).toString();
    ++m_busy;
    emit filesBusyChanged();
    QPointer<ProjectController> self(this);
    QThread *thread = QThread::create([self, projectId, path, office, pdftotext]()
    {
        const FileImport::Result result = FileImport::convert(path, office, pdftotext);
        // Back on the main thread, where it is safe to look at `self`.
        QMetaObject::invokeMethod(
            qApp,
            [self, projectId, result]()
            {
                if (self)
                    self->finishConversion(projectId, result);
            },
            Qt::QueuedConnection);
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ProjectController::finishConversion(const QString &projectId, const FileImport::Result &result)
{
    if (m_busy > 0)
        --m_busy;
    emit filesBusyChanged();
    // The project may have been deleted while this was running.
    if (m_store->project(projectId).id.isEmpty())
        return;
    if (!result.error.isEmpty())
    {
        appendFilesError(result.error);
        return;
    }
    for (const FileImport::Item &item : result.items)
    {
        if (!storeFile(projectId, item.filename, item.data))
            appendFilesError(QStringLiteral("Can't add `%1`: it could not be saved.").arg(item.filename));
    }
}

void ProjectController::addFileFromContent(const QString &filename, const QByteArray &data)
{
    if (m_currentId.isEmpty() || filename.trimmed().isEmpty())
        return;
    if (!storeFile(m_currentId, filename, data))
        appendFilesError(QStringLiteral("Can't add `%1`: it could not be saved.").arg(filename));
}

bool ProjectController::storeFile(const QString &projectId, const QString &filename, const QByteArray &data)
{
    const QString dir = projectDir(projectId);
    const QString safe = QFileInfo(filename).fileName();
    if (dir.isEmpty() || safe.trimmed().isEmpty())
        return false;
    QDir().mkpath(dir);
    const QString dest = dir + QLatin1Char('/') + safe;
    QSaveFile out(dest); // all or nothing: never leaves half a file behind
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
        return false;
    registerFile(projectId, safe, dest);
    return true;
}

// Text files are copied, not read into memory: a log can be far bigger than RAM
// is worth spending on, and only the start of it is ever sent to the model.
bool ProjectController::storeCopy(const QString &projectId, const QString &sourcePath)
{
    const QString dir = projectDir(projectId);
    const QString safe = QFileInfo(sourcePath).fileName();
    if (dir.isEmpty() || safe.trimmed().isEmpty())
        return false;
    QDir().mkpath(dir);
    const QString dest = dir + QLatin1Char('/') + safe;
    const QString canonSource = QFileInfo(sourcePath).canonicalFilePath();
    // Adding a file that is already the stored copy: nothing to copy.
    if (canonSource.isEmpty() || canonSource != QFileInfo(dest).canonicalFilePath())
    {
        QFile::remove(dest);
        if (!QFile::copy(sourcePath, dest))
            return false;
    }
    registerFile(projectId, safe, dest);
    return true;
}

void ProjectController::registerFile(const QString &projectId, const QString &filename, const QString &dest)
{
    ProjectFile f;
    f.id = newId();
    for (const ProjectFile &existing : m_store->projectFiles(projectId))
    {
        if (existing.filename == filename)
        {
            f.id = existing.id; // adding it again replaces it
            break;
        }
    }
    f.projectId = projectId;
    f.filename = filename;
    f.mime = QMimeDatabase().mimeTypeForFile(dest).name();
    f.path = dest;
    f.size = QFileInfo(dest).size();
    m_store->upsertProjectFile(f);
    Project p = m_store->project(projectId);
    p.updatedAt = nowMs();
    m_store->upsertProject(p);
    if (projectId == m_currentId)
        reloadFiles();
}

void ProjectController::removeStoredFile(const QString &path)
{
    const QString canon = QFileInfo(path).canonicalFilePath(); // empty if it is already gone
    const QString root = QDir(projectsRoot()).canonicalPath();
    if (canon.isEmpty() || root.isEmpty() || !canon.startsWith(root + QLatin1Char('/')))
        return;
    QFile::remove(canon);
}

void ProjectController::removeProjectDir(const QString &projectId)
{
    const QString dir = projectDir(projectId);
    if (dir.isEmpty())
        return;
    const QString canon = QDir(dir).canonicalPath();
    const QString root = QDir(projectsRoot()).canonicalPath();
    if (canon.isEmpty() || root.isEmpty() || !canon.startsWith(root + QLatin1Char('/')))
        return;
    QDir(canon).removeRecursively();
}

void ProjectController::removeFile(const QString &fileId)
{
    QString path;
    for (const ProjectFile &f : m_store->projectFiles(m_currentId))
    {
        if (f.id == fileId)
        {
            path = f.path;
            break;
        }
    }
    m_store->deleteProjectFile(fileId);
    if (!path.isEmpty())
        removeStoredFile(path);
    reloadFiles();
}

QString ProjectController::projectContextFor(const QString &projectId, int contextTokens, int *included,
                                             int *truncated, int *omitted) const
{
    if (included)
        *included = 0;
    if (truncated)
        *truncated = 0;
    if (omitted)
        *omitted = 0;
    if (projectId.isEmpty())
        return {};
    const qint64 budget = preloadBudgetBytes(contextTokens);
    const qint64 perFile = perFileLimitBytes(budget);
    // The instructions are sent whole and share the same budget.
    qint64 remaining = qMax<qint64>(0, budget - m_store->project(projectId).instructions.toUtf8().size());
    QString out;
    QStringList leftOut;
    for (const ProjectFile &f : m_store->projectFiles(projectId))
    {
        // Stored before files were converted on the way in: name it, don't paste it.
        if (SpreadsheetExtract::isSpreadsheetPath(f.path))
        {
            out += QStringLiteral("\n## %1\n(not included: a spreadsheet that was never converted. Remove it and add it "
                                  "again to convert it to CSV.)\n")
                       .arg(f.filename);
            continue;
        }
        QFile file(f.path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QByteArray data = file.read(perFile + 1);
        if (!Attach::looksLikeText(data.left(4096)))
        {
            out += QStringLiteral("\n## %1\n(not included: this file is not text. Remove it and add it again to convert "
                                  "it.)\n")
                       .arg(f.filename);
            continue;
        }
        if (remaining <= 0)
        {
            leftOut << f.filename;
            continue;
        }
        bool cut = data.size() > perFile;
        if (cut)
            data.truncate(perFile);
        if (data.size() > remaining)
        {
            data.truncate(remaining);
            cut = true;
        }
        if (cut)
            trimToUtf8Boundary(data);
        remaining -= data.size();
        out += QStringLiteral("\n## %1\n```\n").arg(f.filename);
        out += QString::fromUtf8(data);
        out += QStringLiteral("\n```\n");
        if (included)
            *included += 1;
        if (cut && truncated)
            *truncated += 1;
    }
    if (!leftOut.isEmpty())
        out += QStringLiteral("\n(Not included, over the size budget: %1)\n").arg(leftOut.join(QStringLiteral(", ")));
    if (omitted)
        *omitted = leftOut.size();
    return out;
}
