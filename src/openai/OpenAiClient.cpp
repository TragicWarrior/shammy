#include "openai/OpenAiClient.h"
#include "openai/ModelCaps.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

OpenAiClient::OpenAiClient(QObject *parent)
    : QObject(parent)
{
}

QString OpenAiClient::normalizeBaseUrl(QString url)
{
    url = url.trimmed();
    while (url.endsWith('/'))
        url.chop(1);
    if (url.endsWith(QLatin1String("/v1")))
        return url;
    return url + QStringLiteral("/v1");
}

QUrl OpenAiClient::chatCompletionsUrl(const QString &baseUrl)
{
    return QUrl(normalizeBaseUrl(baseUrl) + QStringLiteral("/chat/completions"));
}

QUrl OpenAiClient::modelsUrl(const QString &baseUrl)
{
    return QUrl(normalizeBaseUrl(baseUrl) + QStringLiteral("/models"));
}

QString OpenAiClient::defaultApiKey(const QString &key)
{
    const QString k = key.trimmed();
    return k.isEmpty() ? QStringLiteral("ollama") : k;
}

QJsonArray OpenAiClient::messagesToJson(const QVector<ChatMessage> &msgs)
{
    QJsonArray arr;
    for (const ChatMessage &m : msgs)
    {
        QJsonObject o;
        o.insert(QStringLiteral("role"), m.role);

        if (m.role == QLatin1String("tool"))
        {
            o.insert(QStringLiteral("content"), m.content);
            if (!m.toolCallId.isEmpty())
                o.insert(QStringLiteral("tool_call_id"), m.toolCallId);
            arr.append(o);
            continue;
        }

        if (!m.toolCallsJson.isEmpty())
        {
            const QJsonDocument doc = QJsonDocument::fromJson(m.toolCallsJson.toUtf8());
            if (doc.isArray())
                o.insert(QStringLiteral("tool_calls"), doc.array());
        }

        QJsonArray parts;
        if (!m.content.isEmpty() || m.attachments.isEmpty())
        {
            bool hasImage = false;
            for (const ContentPart &p : m.attachments)
            {
                if (p.type == QLatin1String("image") && !p.imageDataUrl.isEmpty())
                {
                    hasImage = true;
                    break;
                }
            }
            if (hasImage)
            {
                if (!m.content.isEmpty())
                {
                    parts.append(QJsonObject
                    {
                        {QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"), m.content},
                    });
                }
                for (const ContentPart &p : m.attachments)
                {
                    if (p.type == QLatin1String("image") && !p.imageDataUrl.isEmpty())
                    {
                        parts.append(QJsonObject
                        {
                            {QStringLiteral("type"), QStringLiteral("image_url")},
                            {QStringLiteral("image_url"),
                             QJsonObject{{QStringLiteral("url"), p.imageDataUrl}}},
                        });
                    }
                }
                o.insert(QStringLiteral("content"), parts);
            }
            else
            {
                o.insert(QStringLiteral("content"), m.content);
            }
        }
        else
        {
            o.insert(QStringLiteral("content"), m.content);
        }
        arr.append(o);
    }
    return arr;
}

QByteArray OpenAiClient::buildChatBody(const ChatRequest &req)
{
    QJsonObject body;
    body.insert(QStringLiteral("model"), req.model);
    body.insert(QStringLiteral("messages"), messagesToJson(req.messages));
    body.insert(QStringLiteral("stream"), req.stream);
    body.insert(QStringLiteral("temperature"), req.temperature);
    if (req.topP > 0.0 && req.topP < 1.0)
        body.insert(QStringLiteral("top_p"), req.topP);
    if (req.maxTokens > 0)
        body.insert(QStringLiteral("max_tokens"), req.maxTokens);
    if (!req.tools.isEmpty())
        body.insert(QStringLiteral("tools"), req.tools);
    // Thinking models (Ollama qwen/deepseek etc.) default to thinking if this is
    // omitted. Always send an explicit value. `think` is Ollama's native flag.
    if (req.reasoningEffort.isEmpty() || req.reasoningEffort == QLatin1String("none"))
    {
        body.insert(QStringLiteral("reasoning_effort"), QStringLiteral("none"));
        body.insert(QStringLiteral("think"), false);
    }
    else
    {
        body.insert(QStringLiteral("reasoning_effort"), req.reasoningEffort);
        body.insert(QStringLiteral("think"), req.reasoningEffort);
    }
    if (req.contextSize > 0)
    {
        QJsonObject options;
        options.insert(QStringLiteral("num_ctx"), req.contextSize);
        body.insert(QStringLiteral("options"), options);
        body.insert(QStringLiteral("num_ctx"), req.contextSize);
    }
    if (req.stream)
    {
        QJsonObject streamOpts;
        streamOpts.insert(QStringLiteral("include_usage"), true);
        body.insert(QStringLiteral("stream_options"), streamOpts);
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void OpenAiClient::applyAuth(QNetworkRequest *req, const QString &apiKey, const QString &extraHeadersJson) const
{
    req->setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req->setRawHeader("Authorization",
                      QByteArray("Bearer ") + defaultApiKey(apiKey).toUtf8());
    req->setRawHeader("Accept", "text/event-stream, application/json");
    if (!extraHeadersJson.isEmpty())
    {
        const QJsonDocument doc = QJsonDocument::fromJson(extraHeadersJson.toUtf8());
        if (doc.isObject())
        {
            const QJsonObject o = doc.object();
            for (auto it = o.begin(); it != o.end(); ++it)
                req->setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
        }
    }
}

void OpenAiClient::listModels(const QString &baseUrl, const QString &apiKey, const QString &backendId)
{
    QNetworkRequest req(modelsUrl(baseUrl));
    applyAuth(&req, apiKey, {});
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, backendId]()
    {
        reply->deleteLater();
        QString err;
        QStringList ids;
        if (reply->error() != QNetworkReply::NoError)
            err = reply->errorString();
        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject())
        {
            const QJsonValue data = doc.object().value(QStringLiteral("data"));
            if (data.isArray())
            {
                for (const auto &v : data.toArray())
                {
                    const QString id = v.toObject().value(QStringLiteral("id")).toString();
                    if (!id.isEmpty())
                        ids.append(id);
                }
            }
        }
        else if (err.isEmpty() && !body.isEmpty())
        {
            err = QStringLiteral("unexpected models response");
        }
        emit modelsListed(backendId, ids, err);
    });
}

void OpenAiClient::probeModel(const QString &baseUrl, const QString &apiKey, const QString &model)
{
    if (model.trimmed().isEmpty() || baseUrl.trimmed().isEmpty())
    {
        return;
    }
    QNetworkRequest nreq(ollamaShowUrl(baseUrl));
    applyAuth(&nreq, apiKey, {});
    nreq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("name"), model);
    QNetworkReply *reply = m_nam.post(nreq, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setProperty("probeModel", model);
    connect(reply, &QNetworkReply::finished, this, [this, reply]()
    {
        reply->deleteLater();
        const QString modelId = reply->property("probeModel").toString();
        const QByteArray raw = reply->readAll();
        ModelCaps caps;
        if (reply->error() == QNetworkReply::NoError)
        {
            caps = capsFromOllamaShow(raw);
        }
        if (!caps.advertised)
        {
            caps = capsFromModelId(modelId);
        }
        emit modelProbed(modelId, caps.vision, caps.tools, caps.thinking, caps.audio, caps.advertised);
    });
}

void OpenAiClient::abort()
{
    abortInternal(true);
    abortComplete();
}

void OpenAiClient::abortComplete()
{
    if (!m_completeReply)
    {
        return;
    }
    m_completeReply->disconnect(this);
    m_completeReply->abort();
    m_completeReply->deleteLater();
    m_completeReply = nullptr;
}

void OpenAiClient::completeChat(const ChatRequest &req)
{
    abortComplete();
    ChatRequest oneShot = req;
    oneShot.stream = false;
    oneShot.tools = {};
    QNetworkRequest nreq(chatCompletionsUrl(oneShot.baseUrl));
    applyAuth(&nreq, oneShot.apiKey, {});
    nreq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    m_completeReply = m_nam.post(nreq, buildChatBody(oneShot));
    connect(m_completeReply, &QNetworkReply::finished, this, &OpenAiClient::onCompleteFinished);
}

QString OpenAiClient::extractCompletionText(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
    {
        return {};
    }
    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    QJsonObject message;
    if (!choices.isEmpty())
    {
        const QJsonObject choice = choices.at(0).toObject();
        message = choice.value(QStringLiteral("message")).toObject();
        const QString legacy = choice.value(QStringLiteral("text")).toString();
        if (!legacy.isEmpty() && message.isEmpty())
        {
            return legacy;
        }
    }
    else
    {
        message = root.value(QStringLiteral("message")).toObject();
    }
    const QJsonValue content = message.value(QStringLiteral("content"));
    if (content.isString())
    {
        return content.toString();
    }
    if (content.isArray())
    {
        QString joined;
        for (const auto &v : content.toArray())
        {
            const QJsonObject part = v.toObject();
            if (part.value(QStringLiteral("type")).toString() == QLatin1String("text")
                || part.contains(QStringLiteral("text")))
            {
                joined += part.value(QStringLiteral("text")).toString();
            }
        }
        return joined;
    }
    return {};
}

void OpenAiClient::onCompleteFinished()
{
    if (!m_completeReply)
    {
        return;
    }
    QNetworkReply *reply = m_completeReply;
    m_completeReply = nullptr;
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (reply->error() == QNetworkReply::OperationCanceledError)
    {
        emit completeFailed(QStringLiteral("aborted"));
        return;
    }
    if (reply->error() != QNetworkReply::NoError)
    {
        emit completeFailed(reply->errorString()
                            + (status ? QStringLiteral(" (HTTP %1)").arg(status) : QString()));
        return;
    }
    const QString text = extractCompletionText(body);
    if (text.trimmed().isEmpty())
    {
        emit completeFailed(QStringLiteral("compaction produced an empty summary"));
        return;
    }
    emit completed(text);
}

void OpenAiClient::abortInternal(bool notify)
{
    if (!m_reply)
        return;
    m_aborted = true;
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
    if (notify)
        emit finished(QStringLiteral("aborted"));
}

void OpenAiClient::clearReply()
{
    if (!m_reply)
        return;
    m_reply->disconnect(this);
    m_reply->deleteLater();
    m_reply = nullptr;
}

static QString errorFromJson(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return {};
    const QJsonValue e = doc.object().value(QStringLiteral("error"));
    if (e.isObject())
        return e.toObject().value(QStringLiteral("message")).toString();
    if (e.isString())
        return e.toString();
    return {};
}

QString OpenAiClient::httpErrorMessage(QNetworkReply *reply, const QByteArray &body, int status)
{
    QString api = errorFromJson(body);
    if (api.isEmpty() && reply)
        api = reply->errorString();
    if (status)
        api += QStringLiteral(" (HTTP %1)").arg(status);
    return api;
}

void OpenAiClient::streamChat(const ChatRequest &req)
{
    abortInternal(false);
    m_parser.reset();
    m_gotDone = false;
    m_aborted = false;
    m_failed = false;
    m_finishReason.clear();
    m_streamBuf.clear();

    QNetworkRequest nreq(chatCompletionsUrl(req.baseUrl));
    applyAuth(&nreq, req.apiKey, {});
    nreq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    m_reply = m_nam.post(nreq, buildChatBody(req));
    connect(m_reply, &QNetworkReply::readyRead, this, &OpenAiClient::onStreamReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OpenAiClient::onStreamFinished);
}

void OpenAiClient::onStreamReadyRead()
{
    if (!m_reply || m_aborted)
        return;
    const auto events = m_parser.feed(m_reply->readAll());
    for (const SseParser::Event &ev : events)
    {
        if (!ev.error.isEmpty())
        {
            if (!m_failed)
            {
                m_failed = true;
                emit failed(ev.error);
            }
            return;
        }
        if (!ev.contentDelta.isEmpty())
            emit chunk(ev.contentDelta);
        if (!ev.reasoningDelta.isEmpty())
            emit reasoning(ev.reasoningDelta);
        for (const auto &tc : ev.toolCalls)
            emit toolCallDelta(tc.index, tc.id, tc.name, tc.argumentsDelta);
        if (!ev.finishReason.isEmpty())
            m_finishReason = ev.finishReason;
        if (ev.hasUsage)
            emit usage(ev.promptTokens, ev.completionTokens, ev.totalTokens);
        if (ev.done)
            m_gotDone = true;
    }
}

void OpenAiClient::onStreamFinished()
{
    if (!m_reply || m_aborted)
        return;
    const QByteArray rest = m_reply->readAll();
    if (!rest.isEmpty())
    {
        const auto events = m_parser.feed(rest + '\n');
        for (const SseParser::Event &ev : events)
        {
            if (!ev.error.isEmpty())
            {
                const QString e = ev.error;
                clearReply();
                if (!m_failed)
                {
                    m_failed = true;
                    emit failed(e);
                }
                return;
            }
            if (!ev.contentDelta.isEmpty())
                emit chunk(ev.contentDelta);
            if (!ev.reasoningDelta.isEmpty())
                emit reasoning(ev.reasoningDelta);
            for (const auto &tc : ev.toolCalls)
                emit toolCallDelta(tc.index, tc.id, tc.name, tc.argumentsDelta);
            if (!ev.finishReason.isEmpty())
                m_finishReason = ev.finishReason;
            if (ev.hasUsage)
                emit usage(ev.promptTokens, ev.completionTokens, ev.totalTokens);
            if (ev.done)
                m_gotDone = true;
        }
    }

    QNetworkReply::NetworkError nerr = m_reply->error();
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray errBody = rest;
    const QString httpErr = httpErrorMessage(m_reply, errBody, status);
    clearReply();

    if (m_failed)
        return;
    if (nerr == QNetworkReply::OperationCanceledError)
        emit finished(QStringLiteral("aborted"));
    else if (nerr != QNetworkReply::NoError && !m_gotDone)
    {
        m_failed = true;
        emit failed(httpErr);
    }
    else
        emit finished(m_finishReason.isEmpty() ? QStringLiteral("stop") : m_finishReason);
}
