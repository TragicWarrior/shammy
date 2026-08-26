#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

inline QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

inline qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

inline QString truncateOneLine(QString s, int maxChars = 48)
{
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s = s.simplified();
    if (s.size() > maxChars)
    {
        s.truncate(maxChars - 1);
        s += QChar(0x2026);
    }
    return s;
}

inline QString formatReplyPrompt(const QString &quote, const QString &text)
{
    QString q = quote;
    q.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    q.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    q = q.trimmed();
    const QString body = text.trimmed();
    if (q.isEmpty())
        return body;
    QString out;
    const QStringList lines = q.split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        out += QLatin1String("> ");
        out += line;
        out += QLatin1Char('\n');
    }
    if (!body.isEmpty())
    {
        out += QLatin1Char('\n');
        out += body;
    }
    return out;
}
