#include "mcp/JsonRpc.h"

#include <QJsonDocument>

static QByteArray compactLine(const QJsonObject &o)
{
    QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact);
    line += '\n';
    return line;
}

QByteArray JsonRpc::encodeRequest(const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    QJsonObject o;
    o.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
        o.insert(QStringLiteral("params"), params);
    return compactLine(o);
}

QByteArray JsonRpc::encodeNotification(const QString &method, const QJsonObject &params)
{
    QJsonObject o;
    o.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    o.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
        o.insert(QStringLiteral("params"), params);
    return compactLine(o);
}

JsonRpc::Message JsonRpc::parseLine(const QByteArray &line)
{
    Message m;
    QByteArray trimmed = line;
    if (trimmed.endsWith('\n'))
        trimmed.chop(1);
    if (trimmed.endsWith('\r'))
        trimmed.chop(1);
    if (trimmed.isEmpty())
    {
        m.parseError = QStringLiteral("empty");
        return m;
    }
    if (trimmed.contains('\n'))
    {
        m.parseError = QStringLiteral("embedded newline");
        return m;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        m.parseError = err.errorString();
        return m;
    }
    const QJsonObject o = doc.object();
    m.valid = true;
    m.id = o.value(QStringLiteral("id"));
    m.method = o.value(QStringLiteral("method")).toString();
    const QJsonValue params = o.value(QStringLiteral("params"));
    if (params.isObject())
        m.params = params.toObject();
    m.result = o.value(QStringLiteral("result"));
    const QJsonValue error = o.value(QStringLiteral("error"));
    if (error.isObject())
        m.error = error.toObject();
    m.isResponse = o.contains(QStringLiteral("result")) || o.contains(QStringLiteral("error"));
    m.isNotification = !m.isResponse && (m.id.isNull() || m.id.isUndefined());
    return m;
}
