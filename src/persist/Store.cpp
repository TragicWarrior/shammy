#include "persist/Store.h"
#include "Util.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>

static Backend backendFromQuery(const QSqlQuery &q)
{
    Backend b;
    b.id = q.value(QStringLiteral("id")).toString();
    b.name = q.value(QStringLiteral("name")).toString();
    b.baseUrl = q.value(QStringLiteral("base_url")).toString();
    b.apiKey = q.value(QStringLiteral("api_key")).toString();
    b.extraHeadersJson = q.value(QStringLiteral("extra_headers_json")).toString();
    const QVariant en = q.value(QStringLiteral("enabled"));
    b.enabled = en.isNull() ? true : en.toInt() != 0;
    b.createdAt = q.value(QStringLiteral("created_at")).toLongLong();
    return b;
}

static Conversation convFromQuery(const QSqlQuery &q)
{
    Conversation c;
    c.id = q.value(QStringLiteral("id")).toString();
    c.projectId = q.value(QStringLiteral("project_id")).toString();
    c.title = q.value(QStringLiteral("title")).toString();
    c.backendId = q.value(QStringLiteral("backend_id")).toString();
    c.model = q.value(QStringLiteral("model")).toString();
    c.reasoningEffort = q.value(QStringLiteral("reasoning_effort")).toString();
    c.pinned = q.value(QStringLiteral("pinned")).toInt() != 0;
    c.createdAt = q.value(QStringLiteral("created_at")).toLongLong();
    c.updatedAt = q.value(QStringLiteral("updated_at")).toLongLong();
    return c;
}

static Message msgFromQuery(const QSqlQuery &q)
{
    Message m;
    m.id = q.value(QStringLiteral("id")).toString();
    m.conversationId = q.value(QStringLiteral("conversation_id")).toString();
    m.role = q.value(QStringLiteral("role")).toString();
    m.content = q.value(QStringLiteral("content")).toString();
    m.reasoning = q.value(QStringLiteral("reasoning")).toString();
    m.toolCallsJson = q.value(QStringLiteral("tool_calls_json")).toString();
    m.toolCallId = q.value(QStringLiteral("tool_call_id")).toString();
    m.createdAt = q.value(QStringLiteral("created_at")).toLongLong();
    return m;
}

static Project projectFromQuery(const QSqlQuery &q)
{
    Project p;
    p.id = q.value(QStringLiteral("id")).toString();
    p.name = q.value(QStringLiteral("name")).toString();
    p.description = q.value(QStringLiteral("description")).toString();
    p.instructions = q.value(QStringLiteral("instructions")).toString();
    p.defaultBackend = q.value(QStringLiteral("default_backend")).toString();
    p.defaultModel = q.value(QStringLiteral("default_model")).toString();
    p.sourceId = q.value(QStringLiteral("source_id")).toString();
    p.createdAt = q.value(QStringLiteral("created_at")).toLongLong();
    p.updatedAt = q.value(QStringLiteral("updated_at")).toLongLong();
    return p;
}

static ProjectFile projectFileFromQuery(const QSqlQuery &q)
{
    ProjectFile f;
    f.id = q.value(QStringLiteral("id")).toString();
    f.projectId = q.value(QStringLiteral("project_id")).toString();
    f.filename = q.value(QStringLiteral("filename")).toString();
    f.mime = q.value(QStringLiteral("mime")).toString();
    f.path = q.value(QStringLiteral("path")).toString();
    f.size = q.value(QStringLiteral("size")).toLongLong();
    return f;
}

static Artifact artifactFromQuery(const QSqlQuery &q)
{
    Artifact a;
    a.id = q.value(QStringLiteral("id")).toString();
    a.conversationId = q.value(QStringLiteral("conversation_id")).toString();
    a.messageId = q.value(QStringLiteral("message_id")).toString();
    a.identifier = q.value(QStringLiteral("identifier")).toString();
    a.title = q.value(QStringLiteral("title")).toString();
    a.type = q.value(QStringLiteral("type")).toString();
    a.language = q.value(QStringLiteral("language")).toString();
    a.content = q.value(QStringLiteral("content")).toString();
    a.version = q.value(QStringLiteral("version")).toInt();
    a.createdAt = q.value(QStringLiteral("created_at")).toLongLong();
    return a;
}

Store::Store(QObject *parent)
    : QObject(parent)
    , m_connName(QStringLiteral("shammy-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

Store::~Store()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connName);
}

static bool tableHasColumn(QSqlDatabase &db, const QString &table, const QString &column)
{
    QSqlQuery info(db);
    info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (info.next())
    {
        if (info.value(1).toString() == column)
            return true;
    }
    return false;
}

bool Store::exec(const QString &sql)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql))
    {
        m_error = q.lastError().text();
        return false;
    }
    return true;
}

bool Store::open(const QString &dbPath)
{
    m_path = dbPath;
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    if (QSqlDatabase::contains(m_connName))
    {
        m_db = QSqlDatabase::database(m_connName);
        m_db.close();
        QSqlDatabase::removeDatabase(m_connName);
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open())
    {
        m_error = m_db.lastError().text();
        return false;
    }
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    return migrate();
}

bool Store::migrate()
{
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)")))
        return false;
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS backends ("
            "id TEXT PRIMARY KEY, name TEXT, base_url TEXT, api_key TEXT,"
            "extra_headers_json TEXT, created_at INTEGER, enabled INTEGER DEFAULT 1)")))
        return false;
    if (!tableHasColumn(m_db, QStringLiteral("backends"), QStringLiteral("enabled")))
    {
        exec(QStringLiteral("ALTER TABLE backends ADD COLUMN enabled INTEGER DEFAULT 1"));
    }
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS projects ("
            "id TEXT PRIMARY KEY, name TEXT, instructions TEXT,"
            "default_backend TEXT, default_model TEXT,"
            "created_at INTEGER, updated_at INTEGER)")))
        return false;
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS project_files ("
            "id TEXT PRIMARY KEY, project_id TEXT, filename TEXT, mime TEXT,"
            "path TEXT, size INTEGER,"
            "FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE)")))
        return false;
    if (!tableHasColumn(m_db, QStringLiteral("projects"), QStringLiteral("description")))
        exec(QStringLiteral("ALTER TABLE projects ADD COLUMN description TEXT"));
    if (!tableHasColumn(m_db, QStringLiteral("projects"), QStringLiteral("source_id")))
        exec(QStringLiteral("ALTER TABLE projects ADD COLUMN source_id TEXT"));
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS conversations ("
            "id TEXT PRIMARY KEY, project_id TEXT, title TEXT, backend_id TEXT,"
            "model TEXT, reasoning_effort TEXT, pinned INTEGER, created_at INTEGER, updated_at INTEGER)")))
        return false;
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id TEXT PRIMARY KEY, conversation_id TEXT, role TEXT, content TEXT,"
            "reasoning TEXT, tool_calls_json TEXT, tool_call_id TEXT, created_at INTEGER,"
            "FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE)")))
        return false;
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS artifacts ("
            "id TEXT PRIMARY KEY, conversation_id TEXT, message_id TEXT,"
            "identifier TEXT, title TEXT, type TEXT, language TEXT, content TEXT,"
            "version INTEGER, created_at INTEGER,"
            "FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE)")))
        return false;
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS mcp_allowlist ("
            "server TEXT, tool TEXT, scope TEXT, PRIMARY KEY(server, tool))")))
        return false;
    if (!exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)")))
        return false;

    if (!tableHasColumn(m_db, QStringLiteral("conversations"), QStringLiteral("reasoning_effort")))
        exec(QStringLiteral("ALTER TABLE conversations ADD COLUMN reasoning_effort TEXT"));

    m_fts = exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS search_idx USING fts5("
        "conversation_id UNINDEXED, text)"));
    if (!m_fts)
    {
        m_error.clear();
        m_fts = false;
    }

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM backends"));
    if (q.next() && q.value(0).toInt() == 0)
    {
        Backend ollama;
        ollama.id = newId();
        ollama.name = QStringLiteral("Ollama");
        ollama.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
        ollama.createdAt = nowMs();
        upsertBackend(ollama);
        Backend llama;
        llama.id = newId();
        llama.name = QStringLiteral("llama.cpp");
        llama.baseUrl = QStringLiteral("http://127.0.0.1:8080/v1");
        llama.createdAt = nowMs();
        upsertBackend(llama);
        setSetting(QStringLiteral("current_backend"), ollama.id);
    }
    return true;
}

QString Store::setting(const QString &key, const QString &def) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return def;
}

void Store::setSetting(const QString &key, const QString &value)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO settings(key,value) VALUES(?,?) "
                             "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}

QList<Backend> Store::backends() const
{
    QList<Backend> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT * FROM backends ORDER BY created_at"));
    while (q.next())
        out.append(backendFromQuery(q));
    return out;
}

Backend Store::backend(const QString &id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM backends WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return backendFromQuery(q);
    return {};
}

void Store::upsertBackend(const Backend &b)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO backends(id,name,base_url,api_key,extra_headers_json,created_at,enabled) "
        "VALUES(?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET name=excluded.name, base_url=excluded.base_url, "
        "api_key=excluded.api_key, extra_headers_json=excluded.extra_headers_json, "
        "enabled=excluded.enabled"));
    q.addBindValue(b.id);
    q.addBindValue(b.name);
    q.addBindValue(b.baseUrl);
    q.addBindValue(b.apiKey);
    q.addBindValue(b.extraHeadersJson);
    q.addBindValue(b.createdAt ? b.createdAt : nowMs());
    q.addBindValue(b.enabled ? 1 : 0);
    q.exec();
    emit backendsChanged();
}

void Store::deleteBackend(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM backends WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
    emit backendsChanged();
}

QList<Project> Store::projects() const
{
    QList<Project> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT * FROM projects ORDER BY updated_at DESC"));
    while (q.next())
        out.append(projectFromQuery(q));
    return out;
}

Project Store::project(const QString &id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM projects WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return projectFromQuery(q);
    return {};
}

Project Store::projectBySourceId(const QString &sourceId) const
{
    if (sourceId.trimmed().isEmpty())
        return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM projects WHERE source_id = ? ORDER BY updated_at DESC LIMIT 1"));
    q.addBindValue(sourceId);
    if (q.exec() && q.next())
        return projectFromQuery(q);
    return {};
}

Project Store::projectByName(const QString &name) const
{
    if (name.trimmed().isEmpty())
        return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT * FROM projects WHERE name = ? AND (source_id IS NULL OR source_id = '') "
        "ORDER BY updated_at DESC LIMIT 1"));
    q.addBindValue(name.trimmed());
    if (q.exec() && q.next())
        return projectFromQuery(q);
    return {};
}

void Store::upsertProject(const Project &p)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO projects(id,name,description,instructions,default_backend,default_model,source_id,created_at,updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET name=excluded.name, description=excluded.description, "
        "instructions=excluded.instructions, "
        "default_backend=excluded.default_backend, default_model=excluded.default_model, "
        "source_id=excluded.source_id, "
        "updated_at=excluded.updated_at"));
    q.addBindValue(p.id);
    q.addBindValue(p.name);
    q.addBindValue(p.description);
    q.addBindValue(p.instructions);
    q.addBindValue(p.defaultBackend);
    q.addBindValue(p.defaultModel);
    q.addBindValue(p.sourceId);
    q.addBindValue(p.createdAt ? p.createdAt : nowMs());
    q.addBindValue(p.updatedAt ? p.updatedAt : nowMs());
    q.exec();
    emit projectsChanged();
}

void Store::deleteProject(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE conversations SET project_id = NULL WHERE project_id = ?"));
    q.addBindValue(id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM project_files WHERE project_id = ?"));
    q.addBindValue(id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM projects WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
    emit projectsChanged();
    emit conversationsChanged();
}

QList<ProjectFile> Store::projectFiles(const QString &projectId) const
{
    QList<ProjectFile> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM project_files WHERE project_id = ? ORDER BY filename"));
    q.addBindValue(projectId);
    q.exec();
    while (q.next())
        out.append(projectFileFromQuery(q));
    return out;
}

void Store::upsertProjectFile(const ProjectFile &f)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO project_files(id,project_id,filename,mime,path,size) VALUES(?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET filename=excluded.filename, mime=excluded.mime, "
        "path=excluded.path, size=excluded.size"));
    q.addBindValue(f.id);
    q.addBindValue(f.projectId);
    q.addBindValue(f.filename);
    q.addBindValue(f.mime);
    q.addBindValue(f.path);
    q.addBindValue(f.size);
    q.exec();
    emit projectsChanged();
}

void Store::deleteProjectFile(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM project_files WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
    emit projectsChanged();
}

QList<Conversation> Store::conversations(const QString &projectId) const
{
    QList<Conversation> out;
    QSqlQuery q(m_db);
    if (projectId.isEmpty())
    {
        q.exec(QStringLiteral("SELECT * FROM conversations ORDER BY pinned DESC, updated_at DESC"));
    }
    else
    {
        q.prepare(QStringLiteral(
            "SELECT * FROM conversations WHERE project_id = ? ORDER BY pinned DESC, updated_at DESC"));
        q.addBindValue(projectId);
        q.exec();
    }
    while (q.next())
        out.append(convFromQuery(q));
    return out;
}

Conversation Store::conversation(const QString &id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM conversations WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return convFromQuery(q);
    return {};
}

void Store::upsertConversation(const Conversation &c)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO conversations(id,project_id,title,backend_id,model,reasoning_effort,pinned,created_at,updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET project_id=excluded.project_id, title=excluded.title, "
        "backend_id=excluded.backend_id, model=excluded.model, "
        "reasoning_effort=excluded.reasoning_effort, pinned=excluded.pinned, "
        "updated_at=excluded.updated_at"));
    q.addBindValue(c.id);
    q.addBindValue(c.projectId.isEmpty() ? QVariant() : c.projectId);
    q.addBindValue(c.title);
    q.addBindValue(c.backendId);
    q.addBindValue(c.model);
    q.addBindValue(c.reasoningEffort);
    q.addBindValue(c.pinned ? 1 : 0);
    q.addBindValue(c.createdAt ? c.createdAt : nowMs());
    q.addBindValue(c.updatedAt ? c.updatedAt : nowMs());
    q.exec();
    emit conversationsChanged();
}

void Store::deleteConversation(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM messages WHERE conversation_id = ?"));
    q.addBindValue(id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM artifacts WHERE conversation_id = ?"));
    q.addBindValue(id);
    q.exec();
    if (m_fts)
    {
        q.prepare(QStringLiteral("DELETE FROM search_idx WHERE conversation_id = ?"));
        q.addBindValue(id);
        q.exec();
    }
    q.prepare(QStringLiteral("DELETE FROM conversations WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
    emit conversationsChanged();
}

QList<Conversation> Store::search(const QString &query) const
{
    if (query.trimmed().isEmpty())
        return conversations();
    QList<Conversation> out;
    QSet<QString> seen;
    auto takeRows = [&](QSqlQuery &q)
    {
        while (q.next())
        {
            const Conversation c = convFromQuery(q);
            if (c.id.isEmpty() || seen.contains(c.id))
                continue;
            seen.insert(c.id);
            out.append(c);
        }
    };
    if (m_fts)
    {
        QString qtext = query.trimmed();
        qtext.replace('"', ' ');
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT c.* FROM conversations c "
            "WHERE c.id IN (SELECT DISTINCT conversation_id FROM search_idx WHERE search_idx MATCH ?)"));
        q.addBindValue(qtext);
        if (q.exec())
            takeRows(q);
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT DISTINCT c.* FROM conversations c LEFT JOIN messages m "
        "ON m.conversation_id = c.id WHERE c.title LIKE ? OR m.content LIKE ? "
        "ORDER BY c.pinned DESC, c.updated_at DESC"));
    const QString like = QStringLiteral("%") + query.trimmed() + QStringLiteral("%");
    q.addBindValue(like);
    q.addBindValue(like);
    if (q.exec())
        takeRows(q);
    return out;
}

QList<Message> Store::messages(const QString &conversationId) const
{
    QList<Message> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM messages WHERE conversation_id = ? ORDER BY created_at"));
    q.addBindValue(conversationId);
    q.exec();
    while (q.next())
        out.append(msgFromQuery(q));
    return out;
}

void Store::indexMessage(const Message &m)
{
    if (!m_fts)
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO search_idx(conversation_id, text) VALUES(?,?)"));
    q.addBindValue(m.conversationId);
    q.addBindValue(m.content);
    q.exec();
}

void Store::upsertMessage(const Message &m)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO messages(id,conversation_id,role,content,reasoning,tool_calls_json,tool_call_id,created_at) "
        "VALUES(?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET content=excluded.content, reasoning=excluded.reasoning, "
        "tool_calls_json=excluded.tool_calls_json, tool_call_id=excluded.tool_call_id"));
    q.addBindValue(m.id);
    q.addBindValue(m.conversationId);
    q.addBindValue(m.role);
    q.addBindValue(m.content);
    q.addBindValue(m.reasoning);
    q.addBindValue(m.toolCallsJson);
    q.addBindValue(m.toolCallId);
    q.addBindValue(m.createdAt ? m.createdAt : nowMs());
    q.exec();
    indexMessage(m);
    emit messagesChanged(m.conversationId);
}

void Store::deleteMessage(const QString &id)
{
    deleteMessages({id});
}

void Store::deleteMessages(const QStringList &ids)
{
    if (ids.isEmpty())
        return;
    QString cid;
    m_db.transaction();
    QSqlQuery q(m_db);
    for (const QString &id : ids)
    {
        if (id.isEmpty())
            continue;
        if (cid.isEmpty())
        {
            q.prepare(QStringLiteral("SELECT conversation_id FROM messages WHERE id = ?"));
            q.addBindValue(id);
            if (q.exec() && q.next())
                cid = q.value(0).toString();
        }
        q.prepare(QStringLiteral("DELETE FROM messages WHERE id = ?"));
        q.addBindValue(id);
        q.exec();
    }
    m_db.commit();
    if (!cid.isEmpty())
    {
        reindexConversation(cid);
        emit messagesChanged(cid);
    }
}

void Store::deleteMessagesFrom(const QString &conversationId, qint64 fromCreatedAt)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM messages WHERE conversation_id = ? AND created_at >= ?"));
    q.addBindValue(conversationId);
    q.addBindValue(fromCreatedAt);
    q.exec();
    reindexConversation(conversationId);
    emit messagesChanged(conversationId);
}

void Store::deleteMessagesForConversation(const QString &conversationId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM messages WHERE conversation_id = ?"));
    q.addBindValue(conversationId);
    q.exec();
    reindexConversation(conversationId);
    emit messagesChanged(conversationId);
}

void Store::reindexConversation(const QString &conversationId)
{
    if (!m_fts)
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM search_idx WHERE conversation_id = ?"));
    q.addBindValue(conversationId);
    q.exec();
    for (const Message &m : messages(conversationId))
        indexMessage(m);
}

QList<Artifact> Store::artifactsForConversation(const QString &conversationId) const
{
    QList<Artifact> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT * FROM artifacts WHERE conversation_id = ? ORDER BY identifier, version"));
    q.addBindValue(conversationId);
    q.exec();
    while (q.next())
        out.append(artifactFromQuery(q));
    return out;
}

QList<Artifact> Store::artifactsForIdentifier(const QString &conversationId, const QString &identifier) const
{
    QList<Artifact> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT * FROM artifacts WHERE conversation_id = ? AND identifier = ? ORDER BY version"));
    q.addBindValue(conversationId);
    q.addBindValue(identifier);
    q.exec();
    while (q.next())
        out.append(artifactFromQuery(q));
    return out;
}

int Store::nextArtifactVersion(const QString &conversationId, const QString &identifier) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(version),0) FROM artifacts WHERE conversation_id = ? AND identifier = ?"));
    q.addBindValue(conversationId);
    q.addBindValue(identifier);
    if (q.exec() && q.next())
        return q.value(0).toInt() + 1;
    return 1;
}

void Store::insertArtifact(const Artifact &a)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO artifacts(id,conversation_id,message_id,identifier,title,type,language,content,version,created_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(a.id);
    q.addBindValue(a.conversationId);
    q.addBindValue(a.messageId);
    q.addBindValue(a.identifier);
    q.addBindValue(a.title);
    q.addBindValue(a.type);
    q.addBindValue(a.language);
    q.addBindValue(a.content);
    q.addBindValue(a.version);
    q.addBindValue(a.createdAt ? a.createdAt : nowMs());
    q.exec();
    emit artifactsChanged(a.conversationId);
}

void Store::deleteArtifactsForConversation(const QString &conversationId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM artifacts WHERE conversation_id = ?"));
    q.addBindValue(conversationId);
    q.exec();
    emit artifactsChanged(conversationId);
}

bool Store::isAlwaysAllowed(const QString &server, const QString &tool) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM mcp_allowlist WHERE server = ? AND tool = ?"));
    q.addBindValue(server);
    q.addBindValue(tool);
    return q.exec() && q.next();
}

void Store::allowAlways(const QString &server, const QString &tool)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO mcp_allowlist(server,tool,scope) VALUES(?,?, 'always') "
        "ON CONFLICT(server,tool) DO NOTHING"));
    q.addBindValue(server);
    q.addBindValue(tool);
    q.exec();
}

void Store::revokeAlways(const QString &server, const QString &tool)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM mcp_allowlist WHERE server = ? AND tool = ?"));
    q.addBindValue(server);
    q.addBindValue(tool);
    q.exec();
}
