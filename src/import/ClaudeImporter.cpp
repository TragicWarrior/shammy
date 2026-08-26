#include "import/ClaudeImporter.h"
#include "import/ClaudeCookies.h"
#include "controllers/ProjectController.h"
#include "controllers/SettingsController.h"
#include "persist/Store.h"
#include "Util.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

static const char *kUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";

ClaudeImporter::ClaudeImporter(ProjectController *projects, Store *store, SettingsController *settings,
                               QObject *parent)
    : QObject(parent)
    , m_projectsCtl(projects)
    , m_store(store)
    , m_settings(settings)
{
}

static qint64 parseIsoMs(const QString &s)
{
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid())
        return nowMs();
    return dt.toUTC().toMSecsSinceEpoch();
}

static void extractParts(const QJsonObject &msg, QString *text, QString *reasoning)
{
    const QJsonValue content = msg.value(QStringLiteral("content"));
    if (content.isArray())
    {
        for (const QJsonValue &v : content.toArray())
        {
            const QJsonObject p = v.toObject();
            const QString type = p.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("thinking"))
            {
                const QString t = p.value(QStringLiteral("thinking")).toString();
                if (!t.isEmpty())
                {
                    if (!reasoning->isEmpty())
                        *reasoning += QLatin1Char('\n');
                    *reasoning += t;
                }
            }
            else if (type == QLatin1String("text") || type.isEmpty())
            {
                const QString t = p.value(QStringLiteral("text")).toString();
                if (!t.isEmpty())
                {
                    if (!text->isEmpty())
                        *text += QLatin1Char('\n');
                    *text += t;
                }
            }
        }
    }
    if (text->isEmpty())
        *text = msg.value(QStringLiteral("text")).toString();
}

void ClaudeImporter::setBusy(bool v)
{
    if (m_busy == v)
        return;
    m_busy = v;
    emit busyChanged();
}

void ClaudeImporter::setError(const QString &s)
{
    if (m_error == s)
        return;
    m_error = s;
    emit errorChanged();
}

void ClaudeImporter::setStatus(const QString &s)
{
    if (m_status == s)
        return;
    m_status = s;
    emit statusChanged();
}

void ClaudeImporter::setImportingId(const QString &id)
{
    if (m_importingId == id)
        return;
    m_importingId = id;
    emit importingIdChanged();
}

void ClaudeImporter::getJson(const QString &path, const std::function<void(QJsonDocument, QString)> &done)
{
    const QUrl url(QStringLiteral("https://claude.ai") + path);
    QNetworkRequest req(url);
    req.setRawHeader("Cookie", m_session.cookieHeader.toUtf8());
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Origin", "https://claude.ai");
    req.setRawHeader("Referer", "https://claude.ai/projects");
    QNetworkReply *reply = m_nam.get(req);
    ++m_pending;
    setBusy(true);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, done]()
    {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        QString err;
        QJsonDocument doc;
        if (reply->error() != QNetworkReply::NoError)
        {
            const QJsonObject o = QJsonDocument::fromJson(body).object();
            const QString api = o.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            err = api.isEmpty() ? reply->errorString() : api;
        }
        else
        {
            doc = QJsonDocument::fromJson(body);
            if (doc.isNull())
                err = QStringLiteral("Claude returned a non-JSON response.");
        }
        done(doc, err);
        --m_pending;
        if (m_pending <= 0)
        {
            m_pending = 0;
            setBusy(false);
        }
    });
}

void ClaudeImporter::refresh()
{
    setError({});
    setStatus(QStringLiteral("Reading Claude Desktop session…"));
    ClaudeSessionCookies cookies;
    QString err;
    if (!loadClaudeSessionCookies(&cookies, &err))
    {
        setStatus({});
        setError(err);
        m_projects.clear();
        emit projectsChanged();
        return;
    }
    m_session.orgId = cookies.orgId;
    m_session.cookieHeader = cookies.header;
    const QString path = QStringLiteral("/api/organizations/%1/projects?limit=100&include_harmony_projects=true")
                             .arg(m_session.orgId);
    getJson(path, [this](const QJsonDocument &doc, const QString &err)
    {
        if (!err.isEmpty())
        {
            setStatus({});
            setError(err.contains(QStringLiteral("authorization"), Qt::CaseInsensitive)
                         ? QStringLiteral("Claude rejected the session. Open Claude Desktop, sign in, and try again.")
                         : err);
            return;
        }
        QJsonArray arr = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("data")).toArray();
        QVariantList rows;
        for (const QJsonValue &v : arr)
        {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("archived_at")).isString() && !o.value(QStringLiteral("archived_at")).toString().isEmpty())
                continue;
            QVariantMap row;
            row.insert(QStringLiteral("uuid"), o.value(QStringLiteral("uuid")).toString());
            row.insert(QStringLiteral("name"), o.value(QStringLiteral("name")).toString());
            row.insert(QStringLiteral("description"), o.value(QStringLiteral("description")).toString());
            rows.append(row);
        }
        m_projects = rows;
        emit projectsChanged();
        setStatus(rows.isEmpty() ? QStringLiteral("No Claude projects found.")
                                 : QStringLiteral("%1 project(s) in Claude.").arg(rows.size()));
        setError({});
    });
}

bool ClaudeImporter::ensureSession(QString *error)
{
    if (!m_session.orgId.isEmpty() && !m_session.cookieHeader.isEmpty())
        return true;
    ClaudeSessionCookies cookies;
    if (!loadClaudeSessionCookies(&cookies, error))
        return false;
    m_session.orgId = cookies.orgId;
    m_session.cookieHeader = cookies.header;
    return true;
}

void ClaudeImporter::finishImport()
{
    QString msg = QStringLiteral("%1 “%2” (%3 file%4, %5 chat%6).")
                      .arg(m_importUpdated ? QStringLiteral("Updated") : QStringLiteral("Imported"),
                           m_importName)
                      .arg(m_importFiles)
                      .arg(m_importFiles == 1 ? QString() : QStringLiteral("s"))
                      .arg(m_importChats)
                      .arg(m_importChats == 1 ? QString() : QStringLiteral("s"));
    if (m_importOverCap)
    {
        msg += QStringLiteral(" Over Shammy’s 256 KB preload cap — extra is stored but not all of it is sent to the model.");
    }
    setStatus(msg);
    setError({});
    m_chatQueue.clear();
    m_importLocalId.clear();
    setImportingId({});
}

void ClaudeImporter::importNextChat()
{
    if (m_chatQueue.isEmpty() || !m_store || m_importLocalId.isEmpty())
    {
        finishImport();
        return;
    }
    const QJsonObject meta = m_chatQueue.takeFirst();
    const QString uuid = meta.value(QStringLiteral("uuid")).toString();
    setStatus(QStringLiteral("Importing chats %1/%2…")
                  .arg(m_importChats + 1)
                  .arg(m_importChatTotal));
    const QString path = QStringLiteral("/api/organizations/%1/chat_conversations/%2?rendering_mode=messages")
                             .arg(m_session.orgId, uuid);
    getJson(path, [this, meta](const QJsonDocument &doc, const QString &err)
    {
        if (err.isEmpty() && doc.isObject() && m_store)
        {
            const QJsonObject o = doc.object();
            Conversation c;
            c.title = o.value(QStringLiteral("name")).toString();
            if (c.title.isEmpty())
                c.title = meta.value(QStringLiteral("name")).toString();
            if (c.title.isEmpty())
                c.title = QStringLiteral("Imported chat");
            const QString claudeId = o.value(QStringLiteral("uuid")).toString();
            c.id = claudeId;
            if (m_store->conversation(claudeId).id.isEmpty())
            {
                for (const Conversation &existing : m_store->conversations(m_importLocalId))
                {
                    if (existing.title == c.title)
                    {
                        c.id = existing.id;
                        break;
                    }
                }
            }
            c.projectId = m_importLocalId;
            if (m_settings)
                c.model = m_settings->currentModel();
            c.pinned = o.value(QStringLiteral("is_starred")).toBool()
                || meta.value(QStringLiteral("is_starred")).toBool();
            c.createdAt = parseIsoMs(o.value(QStringLiteral("created_at")).toString());
            c.updatedAt = parseIsoMs(o.value(QStringLiteral("updated_at")).toString());
            m_store->upsertConversation(c);
            m_store->deleteMessagesForConversation(c.id);
            const QJsonArray msgs = o.value(QStringLiteral("chat_messages")).toArray();
            int i = 0;
            for (const QJsonValue &mv : msgs)
            {
                const QJsonObject mo = mv.toObject();
                const QString sender = mo.value(QStringLiteral("sender")).toString();
                Message m;
                m.id = mo.value(QStringLiteral("uuid")).toString();
                if (m.id.isEmpty())
                    m.id = newId();
                m.conversationId = c.id;
                if (sender == QLatin1String("human"))
                    m.role = QStringLiteral("user");
                else if (sender == QLatin1String("assistant"))
                    m.role = QStringLiteral("assistant");
                else
                    continue;
                extractParts(mo, &m.content, &m.reasoning);
                if (m.content.isEmpty() && m.reasoning.isEmpty())
                    continue;
                m.createdAt = parseIsoMs(mo.value(QStringLiteral("created_at")).toString());
                if (!m.createdAt)
                    m.createdAt = c.createdAt + i;
                m_store->upsertMessage(m);
                ++i;
            }
            ++m_importChats;
        }
        importNextChat();
    });
}

void ClaudeImporter::importProject(const QString &uuid)
{
    if (uuid.isEmpty() || !m_projectsCtl)
        return;
    setError({});
    setImportingId(uuid);
    setStatus(QStringLiteral("Importing…"));
    QString err;
    if (!ensureSession(&err))
    {
        setImportingId({});
        setStatus({});
        setError(err);
        return;
    }
    const QString detail = QStringLiteral("/api/organizations/%1/projects/%2").arg(m_session.orgId, uuid);
    getJson(detail, [this, uuid](const QJsonDocument &doc, const QString &err)
    {
        if (!err.isEmpty() || !doc.isObject())
        {
            setImportingId({});
            setStatus({});
            setError(err.isEmpty() ? QStringLiteral("Could not load that Claude project.") : err);
            return;
        }
        const QJsonObject o = doc.object();
        const QString name = o.value(QStringLiteral("name")).toString();
        const QString description = o.value(QStringLiteral("description")).toString();
        const QString prompt = o.value(QStringLiteral("prompt_template")).toString();
        const QString docsPath = QStringLiteral("/api/organizations/%1/projects/%2/docs").arg(m_session.orgId, uuid);
        getJson(docsPath, [this, uuid, name, description, prompt](const QJsonDocument &doc, const QString &err)
        {
            if (!err.isEmpty())
            {
                setImportingId({});
                setStatus({});
                setError(err);
                return;
            }
            QJsonArray files = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("data")).toArray();
            const bool existed = !m_store->projectBySourceId(uuid).id.isEmpty()
                || !m_store->projectByName(name).id.isEmpty();
            m_importLocalId = m_projectsCtl->ensureImportedProject(uuid, name, description);
            m_importUpdated = existed;
            m_importName = name;
            m_importFiles = 0;
            m_importChats = 0;
            m_importOverCap = false;
            m_chatQueue.clear();
            if (!prompt.trimmed().isEmpty())
                m_projectsCtl->setInstructions(prompt);
            qint64 bytes = prompt.toUtf8().size();
            for (const QJsonValue &v : files)
            {
                const QJsonObject f = v.toObject();
                const QString filename = f.value(QStringLiteral("file_name")).toString();
                const QString content = f.value(QStringLiteral("content")).toString();
                if (filename.isEmpty())
                    continue;
                m_projectsCtl->addFileFromContent(filename, content.toUtf8());
                bytes += content.toUtf8().size();
                ++m_importFiles;
            }
            m_importOverCap = bytes > 256 * 1024;
            const QString convPath = QStringLiteral("/api/organizations/%1/projects/%2/conversations")
                                         .arg(m_session.orgId, uuid);
            getJson(convPath, [this](const QJsonDocument &doc, const QString &err)
            {
                if (!err.isEmpty())
                {
                    finishImport();
                    if (m_importChats == 0)
                        setStatus(status() + QStringLiteral(" Chats could not be loaded: %1").arg(err));
                    return;
                }
                QJsonArray arr = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("data")).toArray();
                for (const QJsonValue &v : arr)
                {
                    const QJsonObject c = v.toObject();
                    if (c.value(QStringLiteral("is_temporary")).toBool())
                        continue;
                    if (c.value(QStringLiteral("uuid")).toString().isEmpty())
                        continue;
                    m_chatQueue.append(c);
                }
                m_importChatTotal = m_chatQueue.size();
                if (m_chatQueue.isEmpty())
                {
                    finishImport();
                    return;
                }
                importNextChat();
            });
        });
    });
}
