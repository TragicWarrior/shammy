#include "artifacts/ContentSplitter.h"
#include "artifacts/ArtifactMarkup.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static bool isMdTableLine(const QString &line)
{
    const QString t = line.trimmed();
    if (t.size() < 3 || !t.startsWith(QLatin1Char('|')))
    {
        return false;
    }
    return t.count(QLatin1Char('|')) >= 2;
}

static QStringList splitCells(const QString &line)
{
    QString t = line.trimmed();
    if (t.startsWith(QLatin1Char('|')))
    {
        t = t.mid(1);
    }
    if (t.endsWith(QLatin1Char('|')))
    {
        t.chop(1);
    }
    QStringList cells;
    const QStringList raw = t.split(QLatin1Char('|'));
    for (const QString &c : raw)
    {
        cells.append(c.trimmed());
    }
    return cells;
}

static bool isSeparatorRow(const QStringList &cells)
{
    if (cells.isEmpty())
    {
        return false;
    }
    for (const QString &c : cells)
    {
        if (!ArtifactMarkup::isMdSeparatorCell(c))
        {
            return false;
        }
    }
    return true;
}

static QString tableToJson(const QString &tableBuf)
{
    QList<QStringList> raw;
    const QStringList lines = tableBuf.split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        if (isMdTableLine(line))
        {
            raw.append(splitCells(line));
        }
    }
    if (raw.isEmpty())
    {
        return {};
    }

    QJsonArray headers;
    int dataStart = 0;
    if (raw.size() >= 2 && isSeparatorRow(raw.at(1)))
    {
        for (const QString &c : raw.at(0))
        {
            headers.append(c);
        }
        dataStart = 2;
    }
    else
    {
        for (const QString &c : raw.at(0))
        {
            headers.append(c);
        }
        dataStart = 1;
    }

    const int cols = headers.size();
    QJsonArray rows;
    for (int i = dataStart; i < raw.size(); ++i)
    {
        QJsonArray row;
        const QStringList &cells = raw.at(i);
        for (int c = 0; c < cols; ++c)
        {
            row.append(c < cells.size() ? cells.at(c) : QString());
        }
        rows.append(row);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("headers"), headers);
    obj.insert(QStringLiteral("rows"), rows);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

static void flushPlain(QVector<ContentPart> *out, QString *textBuf)
{
    if (textBuf->isEmpty())
    {
        return;
    }
    ContentPart p;
    p.type = QStringLiteral("text");
    p.text = *textBuf;
    out->push_back(p);
    textBuf->clear();
}

static void flushTable(QVector<ContentPart> *out, QString *tableBuf)
{
    if (tableBuf->isEmpty())
    {
        return;
    }
    ContentPart p;
    p.type = QStringLiteral("table");
    p.language = QStringLiteral("table");
    p.text = tableToJson(*tableBuf);
    if (p.text.isEmpty())
    {
        p.type = QStringLiteral("code");
        p.text = *tableBuf;
    }
    out->push_back(p);
    tableBuf->clear();
}

static void flushText(QVector<ContentPart> *out, QString *acc)
{
    if (acc->isEmpty())
    {
        return;
    }
    const QStringList lines = acc->split(QLatin1Char('\n'));
    QString textBuf;
    QString tableBuf;
    for (int i = 0; i < lines.size(); ++i)
    {
        if (isMdTableLine(lines.at(i)))
        {
            flushPlain(out, &textBuf);
            if (!tableBuf.isEmpty())
            {
                tableBuf += QLatin1Char('\n');
            }
            tableBuf += lines.at(i);
        }
        else
        {
            flushTable(out, &tableBuf);
            if (!textBuf.isEmpty())
            {
                textBuf += QLatin1Char('\n');
            }
            textBuf += lines.at(i);
        }
    }
    flushPlain(out, &textBuf);
    flushTable(out, &tableBuf);
    acc->clear();
}

QVector<ContentPart> ContentSplitter::split(const QString &content)
{
    QVector<ContentPart> out;
    QString acc;
    const QRegularExpression &artifactRe = ArtifactMarkup::artifactTagRe();
    const QRegularExpression &fenceRe = ArtifactMarkup::fenceRe();

    int i = 0;
    const int n = content.size();
    auto firstFrom = [](const QRegularExpression &re, const QString &s, int from)
    {
        auto it = re.globalMatch(s, from);
        return it.hasNext() ? it.next() : QRegularExpressionMatch{};
    };
    while (i < n)
    {
        const auto a = firstFrom(artifactRe, content, i);
        const auto f = firstFrom(fenceRe, content, i);
        int nextA = a.hasMatch() ? a.capturedStart() : n + 1;
        int nextF = f.hasMatch() ? f.capturedStart() : n + 1;

        if (a.hasMatch() && nextA < nextF)
        {
            acc += content.mid(i, a.capturedStart() - i);
            flushText(&out, &acc);
            ContentPart p;
            p.type = QStringLiteral("artifact");
            p.text = a.captured(2).trimmed();
            const QString attrs = a.captured(1);
            p.identifier = ArtifactMarkup::attr(attrs, "identifier");
            p.title = ArtifactMarkup::attr(attrs, "title");
            p.mime = ArtifactMarkup::attr(attrs, "type");
            p.language = ArtifactMarkup::attr(attrs, "language");
            out.push_back(p);
            i = a.capturedEnd();
            continue;
        }
        if (f.hasMatch() && nextF < nextA)
        {
            acc += content.mid(i, f.capturedStart() - i);
            flushText(&out, &acc);
            ContentPart p;
            p.type = QStringLiteral("code");
            p.language = f.captured(1).trimmed();
            p.text = f.captured(2);
            if (p.text.endsWith(QLatin1Char('\n')))
            {
                p.text.chop(1);
            }
            out.push_back(p);
            i = f.capturedEnd();
            continue;
        }
        acc += content.mid(i);
        break;
    }
    flushText(&out, &acc);
    if (out.isEmpty() && !content.isEmpty())
    {
        ContentPart p;
        p.type = QStringLiteral("text");
        p.text = content;
        out.push_back(p);
    }
    return out;
}
