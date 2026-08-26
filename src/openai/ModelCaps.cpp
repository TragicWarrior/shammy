#include "openai/ModelCaps.h"

#include "openai/OpenAiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static bool hasToken(const QString &n, const QString &tok)
{
    return n.contains(tok);
}

ModelCaps capsFromModelId(const QString &id)
{
    ModelCaps c;
    const QString n = id.trimmed().toLower();
    if (n.isEmpty())
    {
        return c;
    }

    c.vision = hasToken(n, QStringLiteral("llava")) || hasToken(n, QStringLiteral("bakllava"))
        || hasToken(n, QStringLiteral("moondream")) || hasToken(n, QStringLiteral("vision"))
        || hasToken(n, QStringLiteral("minicpm-v")) || hasToken(n, QStringLiteral("qwen2-vl"))
        || hasToken(n, QStringLiteral("qwen2.5-vl")) || hasToken(n, QStringLiteral("qwen2.5vl"))
        || hasToken(n, QStringLiteral("pixtral")) || n.contains(QStringLiteral("-vl"))
        || n.contains(QStringLiteral("vl-")) || n.contains(QStringLiteral(":vl"));

    c.audio = hasToken(n, QStringLiteral("whisper")) || hasToken(n, QStringLiteral("qwen2-audio"))
        || hasToken(n, QStringLiteral("qwen-audio")) || hasToken(n, QStringLiteral("-audio"));

    c.thinking = hasToken(n, QStringLiteral("deepseek-r1")) || hasToken(n, QStringLiteral("qwq"))
        || hasToken(n, QStringLiteral("thinking")) || hasToken(n, QStringLiteral("reasoner"))
        || hasToken(n, QStringLiteral("reasoning")) || n.contains(QStringLiteral("r1-"))
        || n.contains(QStringLiteral("-r1")) || hasToken(n, QStringLiteral("cogito"));

    c.tools = hasToken(n, QStringLiteral("llama3.1")) || hasToken(n, QStringLiteral("llama3.2"))
        || hasToken(n, QStringLiteral("llama3.3")) || hasToken(n, QStringLiteral("llama4"))
        || hasToken(n, QStringLiteral("qwen2.5")) || hasToken(n, QStringLiteral("qwen3"))
        || hasToken(n, QStringLiteral("mistral")) || hasToken(n, QStringLiteral("mixtral"))
        || hasToken(n, QStringLiteral("command-r")) || hasToken(n, QStringLiteral("firefunction"))
        || hasToken(n, QStringLiteral("hermes")) || hasToken(n, QStringLiteral("gpt-oss"))
        || hasToken(n, QStringLiteral("kimi"));

    c.advertised = false;
    return c;
}

ModelCaps capsFromOllamaShow(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
    {
        return {};
    }
    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("error")))
    {
        return {};
    }

    ModelCaps c;
    if (root.contains(QStringLiteral("capabilities")))
    {
        c.advertised = true;
        const QJsonArray arr = root.value(QStringLiteral("capabilities")).toArray();
        for (const auto &v : arr)
        {
            const QString s = v.toString().trimmed().toLower();
            if (s == QLatin1String("vision"))
            {
                c.vision = true;
            }
            else if (s == QLatin1String("tools") || s == QLatin1String("tool"))
            {
                c.tools = true;
            }
            else if (s == QLatin1String("thinking") || s == QLatin1String("think"))
            {
                c.thinking = true;
            }
            else if (s == QLatin1String("audio"))
            {
                c.audio = true;
            }
        }
    }

    if (!root.value(QStringLiteral("projector_info")).toObject().isEmpty()
        || root.contains(QStringLiteral("projector")))
    {
        c.vision = true;
        c.advertised = true;
    }

    return c;
}

QUrl ollamaShowUrl(const QString &baseUrl)
{
    QString u = OpenAiClient::normalizeBaseUrl(baseUrl);
    if (u.endsWith(QLatin1String("/v1")))
    {
        u.chop(3);
    }
    return QUrl(u + QStringLiteral("/api/show"));
}
