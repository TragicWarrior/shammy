#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

struct ContentPart
{
    QString type;     // text | code | artifact | image
    QString text;
    QString language;
    QString identifier;
    QString title;
    QString mime;
    QString imageDataUrl;
};

struct ChatMessage
{
    QString id;
    QString conversationId;
    QString role; // system | user | assistant | tool
    QString content;
    QString reasoning;
    QString toolCallsJson;
    QString toolCallId;
    QVector<ContentPart> attachments; // outgoing image/text files
    bool streaming = false;
    QString error;
    qint64 createdAt = 0;
    mutable QVariantList cachedParts;
    mutable bool partsReady = false;
};

using Message = ChatMessage;

struct ChatRequest
{
    QString baseUrl;
    QString apiKey;
    QString model;
    QVector<ChatMessage> messages;
    QJsonArray tools;
    double temperature = 0.7;
    double topP = 1.0;
    int maxTokens = 0;
    int contextSize = 16384;
    QString reasoningEffort;
    bool stream = true;
};

struct ModelCaps
{
    bool vision = false;
    bool tools = false;
    bool thinking = false;
    bool audio = false;
    bool advertised = false;
};

struct Backend
{
    QString id;
    QString name;
    QString baseUrl;
    QString apiKey;
    QString extraHeadersJson;
    qint64 createdAt = 0;
};

struct Conversation
{
    QString id;
    QString projectId;
    QString title;
    QString backendId;
    QString model;
    QString reasoningEffort;
    bool pinned = false;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

struct Project
{
    QString id;
    QString name;
    QString description;
    QString instructions;
    QString defaultBackend;
    QString defaultModel;
    QString sourceId;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    int conversationCount = 0;
    int fileCount = 0;
    qint64 filesBytes = 0;
};

struct ProjectFile
{
    QString id;
    QString projectId;
    QString filename;
    QString mime;
    QString path;
    qint64 size = 0;
};

struct Artifact
{
    QString id;
    QString conversationId;
    QString messageId;
    QString identifier;
    QString title;
    QString type;
    QString language;
    QString content;
    int version = 1;
    qint64 createdAt = 0;
};

struct ArtifactDraft
{
    QString identifier;
    QString title;
    QString type;
    QString language;
    QString content;
};

struct McpServerConfig
{
    QString name;
    QString command;
    QStringList args;
    QMap<QString, QString> env;
    QString cwd;
    bool enabled = true;
    QJsonObject extra;
};

struct McpTool
{
    QString server;
    QString name;         // original MCP name
    QString exposedName;  // name sent to the model
    QString description;
    QJsonObject inputSchema;
};
