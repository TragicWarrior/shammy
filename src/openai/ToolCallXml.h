#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

namespace ToolCallXml
{

inline bool looksLike(const QString &content)
{
    return content.contains(QLatin1String("<tool_call"), Qt::CaseInsensitive)
        || content.contains(QLatin1String("<function="), Qt::CaseInsensitive)
        || content.contains(QLatin1String("<function "), Qt::CaseInsensitive);
}

// Models that ignore the OpenAI tool_calls API often dump XML into content:
//   <tool_call> <function=clover_metrics> <parameter=groupby> hour_of_day
inline QJsonArray parse(const QString &content)
{
    QJsonArray out;
    if (!looksLike(content))
        return out;

    static const QRegularExpression fnRe(
        QStringLiteral("<function\\s*=\\s*([^>\\s/]+)|<function\\b[^>]*\\bname\\s*=\\s*\"([^\"]+)\""),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression paramRe(
        QStringLiteral("<parameter\\s*=\\s*([^>\\s]+)>([^<]*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression namedParamRe(
        QStringLiteral("<parameter\\b[^>]*\\bname\\s*=\\s*\"([^\"]+)\"[^>]*>([^<]*)"),
        QRegularExpression::CaseInsensitiveOption);

    int from = 0;
    int n = 0;
    while (from < content.size())
    {
        const auto fm = fnRe.match(content, from);
        if (!fm.hasMatch())
            break;
        const QString name = fm.captured(1).isEmpty() ? fm.captured(2) : fm.captured(1);
        const int start = fm.capturedEnd();
        const auto next = fnRe.match(content, start);
        const int end = next.hasMatch() ? next.capturedStart() : content.size();
        const QString block = content.mid(start, end - start);
        from = next.hasMatch() ? next.capturedStart() : content.size();

        QJsonObject args;
        auto addParams = [&](const QRegularExpression &re)
        {
            auto it = re.globalMatch(block);
            while (it.hasNext())
            {
                const auto m = it.next();
                const QString key = m.captured(1).trimmed();
                if (!key.isEmpty())
                    args.insert(key, m.captured(2).trimmed());
            }
        };
        addParams(paramRe);
        addParams(namedParamRe);

        QJsonObject fn;
        fn.insert(QStringLiteral("name"), name.trimmed());
        fn.insert(QStringLiteral("arguments"),
                  QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
        QJsonObject call;
        call.insert(QStringLiteral("id"), QStringLiteral("xml_call_%1").arg(++n));
        call.insert(QStringLiteral("type"), QStringLiteral("function"));
        call.insert(QStringLiteral("function"), fn);
        out.append(call);
        if (!next.hasMatch())
            break;
    }
    return out;
}

} // namespace ToolCallXml
