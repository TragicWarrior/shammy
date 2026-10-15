#pragma once

#include "openai/ChatTypes.h"
#include "openai/SseParser.h"

#include <QByteArray>
#include <QJsonArray>
#include <QUrl>
#include <QVector>

// Ollama's own chat API (/api/chat), used instead of its OpenAI-compatible
// /v1/chat/completions when a backend is Ollama. That route drops `num_ctx`,
// so the model would load with Ollama's default context, whatever the size set
// in Shammy; here it goes in `options` and is honored.
namespace OllamaNative
{
QUrl chatUrl(const QString &baseUrl);
QUrl versionUrl(const QString &baseUrl);

// Messages in Ollama's shape: images as bare base64 in `images`, tool-call
// arguments as JSON objects, tool results carrying the tool's name.
QJsonArray messagesToJson(const QVector<ChatMessage> &msgs);
QByteArray buildChatBody(const ChatRequest &req);

// Reads the streamed reply (one JSON object per line) into the same events as
// the OpenAI stream, so the rest of the client does not tell the two apart.
class StreamParser
{
public:
    QVector<SseParser::Event> feed(const QByteArray &data);
    // Parses a last line that never got its newline.
    QVector<SseParser::Event> flush();
    void reset();

private:
    SseParser::Event parseLine(const QByteArray &line);
    QByteArray m_buf;
    int m_nextToolIndex = 0;
    bool m_sawToolCalls = false;
};
} // namespace OllamaNative
