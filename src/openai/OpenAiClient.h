#pragma once

#include "openai/ChatTypes.h"
#include "openai/SseParser.h"

#include <QByteArray>
#include <QObject>
#include <QNetworkAccessManager>

class QNetworkReply;

class OpenAiClient : public QObject
{
    Q_OBJECT
public:
    explicit OpenAiClient(QObject *parent = nullptr);

    static QString normalizeBaseUrl(QString url);
    static QUrl chatCompletionsUrl(const QString &baseUrl);
    static QUrl modelsUrl(const QString &baseUrl);
    static QJsonArray messagesToJson(const QVector<ChatMessage> &msgs);
    static QByteArray buildChatBody(const ChatRequest &req);
    static QString defaultApiKey(const QString &key);

    void listModels(const QString &baseUrl, const QString &apiKey);
    void probeModel(const QString &baseUrl, const QString &apiKey, const QString &model);
    void streamChat(const ChatRequest &req);
    void completeChat(const ChatRequest &req);
    void abort();
    void abortComplete();
    bool busy() const { return m_reply != nullptr || m_completeReply != nullptr; }

signals:
    void modelsListed(const QStringList &ids, const QString &error);
    void modelProbed(const QString &model, bool vision, bool tools, bool thinking, bool audio,
                     bool advertised);
    void chunk(const QString &text);
    void reasoning(const QString &text);
    void toolCallDelta(int index, const QString &id, const QString &name, const QString &argsDelta);
    void finished(const QString &finishReason);
    void failed(const QString &error);
    void usage(int promptTokens, int completionTokens, int totalTokens);
    void completed(const QString &text);
    void completeFailed(const QString &error);

private:
    void applyAuth(QNetworkRequest *req, const QString &apiKey, const QString &extraHeadersJson) const;
    void onStreamReadyRead();
    void onStreamFinished();
    void clearReply();
    void abortInternal(bool notify);

    void onCompleteFinished();
    static QString extractCompletionText(const QByteArray &body);
    static QString httpErrorMessage(QNetworkReply *reply, const QByteArray &body, int status);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QNetworkReply *m_completeReply = nullptr;
    SseParser m_parser;
    bool m_gotDone = false;
    bool m_aborted = false;
    bool m_failed = false;
    QString m_finishReason;
    QByteArray m_streamBuf;
};
