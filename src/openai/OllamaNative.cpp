#include "openai/OllamaNative.h"
#include "openai/OpenAiClient.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace OllamaNative
{
namespace
{
QString rootUrl(const QString &baseUrl)
{
    QString u = OpenAiClient::normalizeBaseUrl(baseUrl);
    u.chop(3); // "/v1"
    return u;
}

// "data:image/png;base64,AAAA" -> "AAAA"
QString base64Of(const QString &dataUrl)
{
    const int comma = dataUrl.indexOf(QLatin1Char(','));
    return comma >= 0 ? dataUrl.mid(comma + 1) : dataUrl;
}

// Thinking: Ollama takes a level only for the models trained with levels
// (gpt-oss); for the rest it is on or off.
QJsonValue thinkValue(const ChatRequest &req)
{
    const QString effort = req.reasoningEffort.trimmed().toLower();
    if (effort.isEmpty() || effort == QLatin1String("none"))
        return false;
    if (req.model.contains(QLatin1String("gpt-oss"), Qt::CaseInsensitive))
        return effort;
    return true;
}
} // namespace

QUrl chatUrl(const QString &baseUrl)
{
    return QUrl(rootUrl(baseUrl) + QStringLiteral("/api/chat"));
}

QUrl versionUrl(const QString &baseUrl)
{
    return QUrl(rootUrl(baseUrl) + QStringLiteral("/api/version"));
}

QJsonArray messagesToJson(const QVector<ChatMessage> &msgs)
{
    QJsonArray arr;
    QHash<QString, QString> toolNames; // call id -> tool name, for the results
    for (const ChatMessage &m : msgs)
    {
        QJsonObject o;
        o.insert(QStringLiteral("role"), m.role);
        o.insert(QStringLiteral("content"), m.content);

        if (m.role == QLatin1String("tool"))
        {
            if (!m.toolCallId.isEmpty())
            {
                o.insert(QStringLiteral("tool_call_id"), m.toolCallId);
                const QString name = toolNames.value(m.toolCallId);
                if (!name.isEmpty())
                    o.insert(QStringLiteral("tool_name"), name);
            }
            arr.append(o);
            continue;
        }

        if (!m.toolCallsJson.isEmpty())
        {
            QJsonArray calls;
            const QJsonArray stored = QJsonDocument::fromJson(m.toolCallsJson.toUtf8()).array();
            for (int i = 0; i < stored.size(); ++i)
            {
                const QJsonObject call = stored.at(i).toObject();
                const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
                const QString name = fn.value(QStringLiteral("name")).toString();
                // Stored as the OpenAI API sends them, a JSON string; Ollama wants the object.
                const QJsonValue rawArgs = fn.value(QStringLiteral("arguments"));
                QJsonObject args = rawArgs.isObject() ? rawArgs.toObject()
                                                       : QJsonDocument::fromJson(rawArgs.toString().toUtf8()).object();
                QJsonObject outFn{{QStringLiteral("index"), i},
                                  {QStringLiteral("name"), name},
                                  {QStringLiteral("arguments"), args}};
                QJsonObject out{{QStringLiteral("function"), outFn}};
                const QString id = call.value(QStringLiteral("id")).toString();
                if (!id.isEmpty())
                {
                    out.insert(QStringLiteral("id"), id);
                    toolNames.insert(id, name);
                }
                calls.append(out);
            }
            if (!calls.isEmpty())
                o.insert(QStringLiteral("tool_calls"), calls);
        }

        QJsonArray images;
        for (const ContentPart &p : m.attachments)
        {
            if (p.type == QLatin1String("image") && !p.imageDataUrl.isEmpty())
                images.append(base64Of(p.imageDataUrl));
        }
        if (!images.isEmpty())
            o.insert(QStringLiteral("images"), images);
        arr.append(o);
    }
    return arr;
}

QByteArray buildChatBody(const ChatRequest &req)
{
    QJsonObject options;
    options.insert(QStringLiteral("temperature"), req.temperature);
    if (req.topP > 0.0 && req.topP < 1.0)
        options.insert(QStringLiteral("top_p"), req.topP);
    if (req.maxTokens > 0)
        options.insert(QStringLiteral("num_predict"), req.maxTokens);
    if (req.contextSize > 0)
        options.insert(QStringLiteral("num_ctx"), req.contextSize);

    QJsonObject body;
    body.insert(QStringLiteral("model"), req.model);
    body.insert(QStringLiteral("messages"), messagesToJson(req.messages));
    body.insert(QStringLiteral("stream"), req.stream);
    body.insert(QStringLiteral("options"), options);
    // Thinking models think unless told not to, so this is always sent.
    body.insert(QStringLiteral("think"), thinkValue(req));
    if (!req.tools.isEmpty())
        body.insert(QStringLiteral("tools"), req.tools);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void StreamParser::reset()
{
    m_buf.clear();
    m_nextToolIndex = 0;
    m_sawToolCalls = false;
}

QVector<SseParser::Event> StreamParser::feed(const QByteArray &data)
{
    m_buf += data;
    QVector<SseParser::Event> out;
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0)
    {
        const QByteArray line = m_buf.left(nl).trimmed();
        m_buf.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        const SseParser::Event ev = parseLine(line);
        if (!ev.isEmpty())
            out << ev;
    }
    return out;
}

QVector<SseParser::Event> StreamParser::flush()
{
    const QByteArray line = m_buf.trimmed();
    m_buf.clear();
    if (line.isEmpty())
        return {};
    const SseParser::Event ev = parseLine(line);
    return ev.isEmpty() ? QVector<SseParser::Event>{} : QVector<SseParser::Event>{ev};
}

SseParser::Event StreamParser::parseLine(const QByteArray &line)
{
    SseParser::Event ev;
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    if (o.isEmpty())
        return ev;
    const QJsonValue err = o.value(QStringLiteral("error"));
    if (err.isString() || err.isObject())
    {
        ev.error = err.isString() ? err.toString() : err.toObject().value(QStringLiteral("message")).toString();
        if (ev.error.isEmpty())
            ev.error = QStringLiteral("the server reported an error");
        return ev;
    }
    const QJsonObject msg = o.value(QStringLiteral("message")).toObject();
    ev.contentDelta = msg.value(QStringLiteral("content")).toString();
    ev.reasoningDelta = msg.value(QStringLiteral("thinking")).toString();
    // Ollama sends each tool call whole, not in pieces.
    for (const QJsonValue &v : msg.value(QStringLiteral("tool_calls")).toArray())
    {
        const QJsonObject call = v.toObject();
        const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
        SseParser::ToolCallDelta d;
        d.index = fn.contains(QStringLiteral("index")) ? fn.value(QStringLiteral("index")).toInt() : m_nextToolIndex;
        m_nextToolIndex = qMax(m_nextToolIndex, d.index + 1);
        d.id = call.value(QStringLiteral("id")).toString();
        d.name = fn.value(QStringLiteral("name")).toString();
        const QJsonValue args = fn.value(QStringLiteral("arguments"));
        d.argumentsDelta = args.isString() ? args.toString()
                                           : QString::fromUtf8(QJsonDocument(args.toObject()).toJson(QJsonDocument::Compact));
        ev.toolCalls << d;
        m_sawToolCalls = true;
    }
    if (o.value(QStringLiteral("done")).toBool())
    {
        ev.done = true;
        QString reason = o.value(QStringLiteral("done_reason")).toString();
        if (m_sawToolCalls && (reason.isEmpty() || reason == QLatin1String("stop")))
            reason = QStringLiteral("tool_calls");
        ev.finishReason = reason.isEmpty() ? QStringLiteral("stop") : reason;
        const int prompt = o.value(QStringLiteral("prompt_eval_count")).toInt();
        const int completion = o.value(QStringLiteral("eval_count")).toInt();
        if (prompt > 0 || completion > 0)
        {
            ev.hasUsage = true;
            ev.promptTokens = prompt;
            ev.completionTokens = completion;
            ev.totalTokens = prompt + completion;
        }
    }
    return ev;
}
} // namespace OllamaNative
