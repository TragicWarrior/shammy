#include "artifacts/ArtifactExtractor.h"
#include "artifacts/ArtifactMarkup.h"

QString ArtifactExtractor::guessMime(const QString &typeOrLang)
{
    const QString t = typeOrLang.trimmed().toLower();
    if (t.contains('/'))
        return t;
    if (t == QLatin1String("html") || t == QLatin1String("htm"))
        return QStringLiteral("text/html");
    if (t == QLatin1String("svg"))
        return QStringLiteral("image/svg+xml");
    if (t == QLatin1String("md") || t == QLatin1String("markdown"))
        return QStringLiteral("text/markdown");
    if (t == QLatin1String("js") || t == QLatin1String("javascript"))
        return QStringLiteral("text/javascript");
    if (t == QLatin1String("css"))
        return QStringLiteral("text/css");
    if (t == QLatin1String("json"))
        return QStringLiteral("application/json");
    if (t == QLatin1String("py") || t == QLatin1String("python"))
        return QStringLiteral("text/x-python");
    if (t == QLatin1String("cpp") || t == QLatin1String("c++") || t == QLatin1String("cc"))
        return QStringLiteral("text/x-c++");
    if (t.isEmpty() || t == QLatin1String("text") || t == QLatin1String("plain"))
        return QStringLiteral("text/plain");
    return QStringLiteral("text/x-%1").arg(t);
}

static int lineCount(const QString &s)
{
    if (s.isEmpty())
        return 0;
    return s.count(QLatin1Char('\n')) + 1;
}

QVector<ArtifactDraft> ArtifactExtractor::extract(const QString &content)
{
    QVector<ArtifactDraft> out;

    int n = 0;
    auto it = ArtifactMarkup::artifactTagRe().globalMatch(content);
    while (it.hasNext())
    {
        const auto m = it.next();
        ArtifactDraft d;
        const QString attrs = m.captured(1);
        d.identifier = ArtifactMarkup::attr(attrs, "identifier");
        d.title = ArtifactMarkup::attr(attrs, "title");
        d.type = guessMime(ArtifactMarkup::attr(attrs, "type"));
        d.language = ArtifactMarkup::attr(attrs, "language");
        d.content = m.captured(2);
        if (d.content.startsWith(QLatin1Char('\n')))
            d.content.remove(0, 1);
        if (d.identifier.isEmpty())
            d.identifier = QStringLiteral("artifact-%1").arg(++n);
        if (d.title.isEmpty())
            d.title = d.identifier;
        out.push_back(d);
    }
    if (!out.isEmpty())
        return out;

    auto fit = ArtifactMarkup::fenceRe().globalMatch(content);
    int k = 0;
    while (fit.hasNext())
    {
        const auto m = fit.next();
        const QString lang = m.captured(1).trimmed().toLower();
        const QString body = m.captured(2);
        if (!ArtifactMarkup::isPromoteLanguage(lang) || lineCount(body) < kFencePromoteMinLines)
            continue;
        ArtifactDraft d;
        d.identifier = QStringLiteral("fence-%1").arg(++k);
        d.title = lang.toUpper();
        d.language = lang;
        d.type = guessMime(lang);
        d.content = body;
        out.push_back(d);
    }
    return out;
}
