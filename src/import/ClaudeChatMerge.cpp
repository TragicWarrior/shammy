#include "import/ClaudeChatMerge.h"
#include "Util.h"
#include "persist/Store.h"

#include <QDateTime>
#include <QJsonArray>
#include <QStringList>

namespace
{
// What imported messages are marked with, so a later import can tell them from
// messages written here.
const QString kSource = QStringLiteral("claude");

qint64 parseIsoMs(const QString &s)
{
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::ISODate);
    return dt.isValid() ? dt.toUTC().toMSecsSinceEpoch() : 0;
}

// Claude's own stand-in for a block its API was not asked to render.
const QLatin1String kUnsupported("This block is not supported on your current device yet.");

// Claude sends it bare or inside a code fence.
bool isUnsupportedNotice(QString t)
{
    t.remove(QLatin1Char('`'));
    return t.trimmed() == kUnsupported;
}

QString oneLine(QString s, int max = 140)
{
    s = s.simplified();
    if (s.size() > max)
    {
        s.truncate(max - 1);
        s += QChar(0x2026);
    }
    return s;
}

// A short note for a tool call Claude made, in place of the call itself: what
// it did, not the data it sent or got back.
QString toolNote(const QJsonObject &block)
{
    const QString name = block.value(QStringLiteral("name")).toString();
    const QJsonObject in = block.value(QStringLiteral("input")).toObject();
    const auto str = [&in](const char *key) { return oneLine(in.value(QLatin1String(key)).toString()); };
    const auto quoted = [](const QString &v) { return QStringLiteral("“%1”").arg(v); };
    const auto code = [](QString v) { return QLatin1Char('`') + v.remove(QLatin1Char('`')) + QLatin1Char('`'); };

    QString what;
    if (name == QLatin1String("web_search") && !str("query").isEmpty())
        what = QStringLiteral("Searched the web for %1").arg(quoted(str("query")));
    else if (name == QLatin1String("image_search") && !str("query").isEmpty())
        what = QStringLiteral("Searched for images of %1").arg(quoted(str("query")));
    else if (name == QLatin1String("conversation_search") && !str("query").isEmpty())
        what = QStringLiteral("Searched past chats for %1").arg(quoted(str("query")));
    else if (name == QLatin1String("web_fetch") && !str("url").isEmpty())
        what = QStringLiteral("Fetched %1").arg(str("url"));
    else if (name == QLatin1String("create_file") && !str("path").isEmpty())
        what = QStringLiteral("Created file %1").arg(code(str("path")));
    else if (name == QLatin1String("str_replace") && !str("path").isEmpty())
        what = QStringLiteral("Edited file %1").arg(code(str("path")));
    else if (name == QLatin1String("view") && !str("path").isEmpty())
        what = QStringLiteral("Viewed %1").arg(code(str("path")));
    else if (name == QLatin1String("present_files"))
    {
        QStringList files;
        for (const QJsonValue &v : in.value(QStringLiteral("filepaths")).toArray())
            files << code(oneLine(v.toString()));
        if (!files.isEmpty())
            what = QStringLiteral("Presented %1").arg(files.join(QStringLiteral(", ")));
    }
    else if (name == QLatin1String("bash_tool"))
    {
        const QString why = str("description");
        what = why.isEmpty() ? QStringLiteral("Ran a command") : QStringLiteral("Ran a command: %1").arg(why);
    }
    else if (name == QLatin1String("artifacts"))
    {
        const QString title = str("title");
        const QString verb = in.value(QStringLiteral("command")).toString() == QLatin1String("create")
            ? QStringLiteral("Created") : QStringLiteral("Updated");
        what = title.isEmpty() ? QStringLiteral("%1 an artifact").arg(verb)
                               : QStringLiteral("%1 artifact %2").arg(verb, quoted(title));
    }
    if (what.isEmpty())
    {
        // Anything else, including connected tools: Claude's own description of
        // the step if it gave one, else the tool's name.
        const QString said = oneLine(block.value(QStringLiteral("message")).toString());
        const QString integration = oneLine(block.value(QStringLiteral("integration_name")).toString());
        if (!said.isEmpty() && !said.contains(QLatin1Char('_')))
            what = said;
        else if (!integration.isEmpty())
            what = QStringLiteral("Used %1 (%2)").arg(integration, code(name));
        else
            what = name.isEmpty() ? QStringLiteral("Used a tool") : QStringLiteral("Used tool %1").arg(code(name));
    }
    return QStringLiteral("*%1*").arg(what);
}

void extractParts(const QJsonObject &msg, QString *text, QString *reasoning)
{
    const QJsonValue content = msg.value(QStringLiteral("content"));
    if (content.isArray())
    {
        bool lastWasNote = false;
        // Notes stand apart as paragraphs of their own; text parts join as before.
        const auto add = [&](const QString &piece, bool note)
        {
            if (!text->isEmpty())
                *text += (note || lastWasNote) ? QStringLiteral("\n\n") : QStringLiteral("\n");
            *text += piece;
            lastWasNote = note;
        };
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
                // The stand-in says nothing; with tool blocks asked for it should
                // not come at all.
                if (!t.isEmpty() && !isUnsupportedNotice(t))
                    add(t, false);
            }
            else if (type == QLatin1String("tool_use"))
            {
                add(toolNote(p), true);
            }
            // tool_result and anything unknown: the reply text already says what
            // came of it.
        }
    }
    if (text->isEmpty())
    {
        const QString flat = msg.value(QStringLiteral("text")).toString();
        if (!isUnsupportedNotice(flat))
            *text = flat;
    }
}
} // namespace

ClaudeChatMerge::Outcome ClaudeChatMerge::merge(Store *store, const QString &projectId, const QString &model,
                                                const QJsonObject &chat, const QJsonObject &meta)
{
    Outcome out;
    const QString claudeId = chat.value(QStringLiteral("uuid")).toString();
    if (!store || claudeId.isEmpty())
        return out;

    const qint64 claudeCreated = parseIsoMs(chat.value(QStringLiteral("created_at")).toString());
    const qint64 claudeUpdated = parseIsoMs(chat.value(QStringLiteral("updated_at")).toString());
    const qint64 base = claudeCreated ? claudeCreated : nowMs();

    QList<Message> incoming;
    QStringList ids;
    int order = 0;
    for (const QJsonValue &mv : chat.value(QStringLiteral("chat_messages")).toArray())
    {
        const QJsonObject mo = mv.toObject();
        const QString sender = mo.value(QStringLiteral("sender")).toString();
        Message m;
        if (sender == QLatin1String("human"))
            m.role = QStringLiteral("user");
        else if (sender == QLatin1String("assistant"))
            m.role = QStringLiteral("assistant");
        else
            continue;
        extractParts(mo, &m.content, &m.reasoning);
        if (m.content.isEmpty() && m.reasoning.isEmpty())
            continue;
        m.id = mo.value(QStringLiteral("uuid")).toString();
        if (m.id.isEmpty())
            m.id = newId();
        // Without a date of its own, keep it in the order Claude gave it.
        m.createdAt = parseIsoMs(mo.value(QStringLiteral("created_at")).toString());
        if (!m.createdAt)
            m.createdAt = base + order;
        ++order;
        ids << m.id;
        incoming.append(m);
    }

    // Which local chat is this? The one with Claude's id, or failing that the
    // one that already holds some of these messages (an import made under
    // another id). Never a chat that only happens to have the same title.
    Conversation c = store->conversation(claudeId);
    if (c.id.isEmpty())
    {
        const QString holder = store->conversationHoldingAnyMessage(ids);
        if (!holder.isEmpty())
            c = store->conversation(holder);
    }
    out.created = c.id.isEmpty();
    if (out.created)
    {
        c.id = claudeId;
        c.title = chat.value(QStringLiteral("name")).toString();
        if (c.title.isEmpty())
            c.title = meta.value(QStringLiteral("name")).toString();
        if (c.title.isEmpty())
            c.title = QStringLiteral("Imported chat");
        c.model = model;
        c.pinned = chat.value(QStringLiteral("is_starred")).toBool() || meta.value(QStringLiteral("is_starred")).toBool();
        c.createdAt = base;
        c.updatedAt = claudeUpdated ? claudeUpdated : base;
    }
    else
    {
        // Its title, model, thinking mode and pin are the user's by now.
        c.updatedAt = qMax(c.updatedAt, claudeUpdated);
    }
    c.projectId = projectId;
    store->upsertConversation(c);

    for (Message &m : incoming)
        m.conversationId = c.id;
    store->mergeImportedMessages(c.id, incoming, kSource);

    out.conversationId = c.id;
    out.messages = incoming.size();
    return out;
}
