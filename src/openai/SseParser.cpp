#include "openai/SseParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

void SseParser::reset()
{
    m_buf.clear();
}

static QByteArray stripPrefix(QByteArray line)
{
    if (line.endsWith('\r'))
        line.chop(1);
    if (line.startsWith("data:"))
    {
        line = line.mid(5);
        if (line.startsWith(' '))
            line = line.mid(1);
    }
    return line;
}

SseParser::Event SseParser::parsePayload(QByteArray payload) const
{
    Event ev;
    payload = payload.trimmed();
    if (payload.isEmpty())
        return ev;
    if (payload == "[DONE]" || payload == "\"[DONE]\"")
    {
        ev.done = true;
        return ev;
    }
    if (!payload.startsWith('{'))
        return ev;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return ev;

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("error")))
    {
        const auto e = root.value(QStringLiteral("error"));
        if (e.isObject())
            ev.error = e.toObject().value(QStringLiteral("message")).toString();
        else
            ev.error = e.toString();
        if (ev.error.isEmpty())
            ev.error = QStringLiteral("backend error");
        return ev;
    }

    const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
    if (!usage.isEmpty())
    {
        ev.hasUsage = true;
        ev.promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt();
        ev.completionTokens = usage.value(QStringLiteral("completion_tokens")).toInt();
        ev.totalTokens = usage.value(QStringLiteral("total_tokens")).toInt();
        if (ev.totalTokens <= 0)
        {
            ev.totalTokens = ev.promptTokens + ev.completionTokens;
        }
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
    {
        return ev;
    }

    const QJsonObject choice = choices.at(0).toObject();
    ev.finishReason = choice.value(QStringLiteral("finish_reason")).toString();
    if (ev.finishReason == QLatin1String("stop")
        || ev.finishReason == QLatin1String("length")
        || ev.finishReason == QLatin1String("tool_calls"))
        {
        // finish_reason often arrives on the last content chunk; not a stream end by itself
    }

    QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    if (delta.isEmpty())
        delta = choice.value(QStringLiteral("message")).toObject();

    ev.contentDelta = delta.value(QStringLiteral("content")).toString();
    if (ev.contentDelta.isEmpty())
        ev.contentDelta = delta.value(QStringLiteral("text")).toString();

    QJsonValue reasoning = delta.value(QStringLiteral("reasoning_content"));
    if (reasoning.isNull() || reasoning.isUndefined())
        reasoning = delta.value(QStringLiteral("reasoning"));
    if (reasoning.isString())
        ev.reasoningDelta = reasoning.toString();
    else if (reasoning.isObject())
        ev.reasoningDelta = reasoning.toObject().value(QStringLiteral("content")).toString();

    if (ev.reasoningDelta.isEmpty())
        ev.reasoningDelta = delta.value(QStringLiteral("thinking")).toString();

    const QJsonArray toolCalls = delta.value(QStringLiteral("tool_calls")).toArray();
    for (const auto &v : toolCalls)
    {
        const QJsonObject tc = v.toObject();
        ToolCallDelta d;
        d.index = tc.value(QStringLiteral("index")).toInt(0);
        d.id = tc.value(QStringLiteral("id")).toString();
        const QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
        d.name = fn.value(QStringLiteral("name")).toString();
        d.argumentsDelta = fn.value(QStringLiteral("arguments")).toString();
        ev.toolCalls.push_back(d);
    }
    return ev;
}

QVector<SseParser::Event> SseParser::feed(const QByteArray &data)
{
    m_buf += data;
    QVector<Event> out;

    while (true)
    {
        const int nl = m_buf.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);

        if (line.startsWith(':'))
            continue;

        if (line.isEmpty())
            continue;

        QByteArray payload;
        if (line.startsWith("data:"))
            payload = stripPrefix(line);
        else if (line.startsWith('{') || line.startsWith('['))
            payload = line;
        else
            continue;

        Event ev = parsePayload(payload);
        if (!ev.isEmpty())
            out.push_back(ev);
    }
    return out;
}
