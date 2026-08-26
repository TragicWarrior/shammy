#pragma once

#include <QRegularExpression>
#include <QString>

namespace ArtifactMarkup
{

inline const QRegularExpression &artifactTagRe()
{
    static const QRegularExpression re(
        QStringLiteral("<artifact\\s+([^>]*)>([\\s\\S]*?)</artifact>"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

inline const QRegularExpression &fenceRe()
{
    static const QRegularExpression re(QStringLiteral("```([^\\n]*)\\n([\\s\\S]*?)```"));
    return re;
}

inline const QRegularExpression &mdSeparatorRe()
{
    static const QRegularExpression re(QStringLiteral("^:?-+:?$"));
    return re;
}

inline QString attr(const QString &attrs, const char *key)
{
    static const QRegularExpression identifierRe(
        QStringLiteral("identifier\\s*=\\s*\"([^\"]*)\""), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression titleRe(
        QStringLiteral("title\\s*=\\s*\"([^\"]*)\""), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression typeRe(
        QStringLiteral("type\\s*=\\s*\"([^\"]*)\""), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression languageRe(
        QStringLiteral("language\\s*=\\s*\"([^\"]*)\""), QRegularExpression::CaseInsensitiveOption);

    const QRegularExpression *re = nullptr;
    if (qstrcmp(key, "identifier") == 0)
        re = &identifierRe;
    else if (qstrcmp(key, "title") == 0)
        re = &titleRe;
    else if (qstrcmp(key, "type") == 0)
        re = &typeRe;
    else if (qstrcmp(key, "language") == 0)
        re = &languageRe;
    else
    {
        const QRegularExpression tmp(QStringLiteral("%1\\s*=\\s*\"([^\"]*)\"").arg(QLatin1String(key)),
                                     QRegularExpression::CaseInsensitiveOption);
        const auto m = tmp.match(attrs);
        return m.hasMatch() ? m.captured(1) : QString();
    }
    const auto m = re->match(attrs);
    return m.hasMatch() ? m.captured(1) : QString();
}

inline bool isPromoteLanguage(const QString &lang)
{
    return lang == QLatin1String("html") || lang == QLatin1String("htm")
        || lang == QLatin1String("svg") || lang == QLatin1String("javascript")
        || lang == QLatin1String("js") || lang == QLatin1String("md")
        || lang == QLatin1String("markdown");
}

inline bool isMdSeparatorCell(QString cell)
{
    cell.remove(QLatin1Char(' '));
    return !cell.isEmpty() && mdSeparatorRe().match(cell).hasMatch();
}

} // namespace ArtifactMarkup
