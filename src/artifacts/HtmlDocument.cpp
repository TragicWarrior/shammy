#include "artifacts/HtmlDocument.h"

#include <QRegularExpression>
#include <QStringView>

static QString escapeAttr(QString s)
{
    s.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    s.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    s.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    s.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return s;
}

static QStringView ltrimmed(QStringView s)
{
    int i = 0;
    while (i < s.size() && s.at(i).isSpace())
        ++i;
    return s.sliced(i);
}

static QString wrapDocument(const QString &title, const QString &style, const QString &body)
{
    return QStringLiteral("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
                          "<meta charset=\"utf-8\">\n"
                          "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                          "<title>")
        + title
        + QStringLiteral("</title>\n<style>")
        + style
        + QStringLiteral("</style>\n</head>\n<body>\n")
        + body + QStringLiteral("\n</body>\n</html>\n");
}

bool HtmlDocument::isHtmlType(const QString &mime)
{
    const QString t = mime.trimmed().toLower();
    return t.contains(QLatin1String("html")) || t == QLatin1String("text/xhtml");
}

bool HtmlDocument::isSvgType(const QString &mime)
{
    const QString t = mime.trimmed().toLower();
    return t.contains(QLatin1String("svg"));
}

bool HtmlDocument::isMarkdownType(const QString &mime)
{
    const QString t = mime.trimmed().toLower();
    return t.contains(QLatin1String("markdown")) || t == QLatin1String("text/x-markdown")
        || t == QLatin1String("text/md") || t.endsWith(QLatin1String("/md"));
}

bool HtmlDocument::looksLikeSvg(const QString &content)
{
    QStringView t = ltrimmed(content);
    if (t.startsWith(QLatin1String("<?xml"), Qt::CaseInsensitive))
    {
        const int gt = t.indexOf(QLatin1Char('>'));
        if (gt < 0)
            return false;
        t = ltrimmed(t.sliced(gt + 1));
    }
    return t.startsWith(QLatin1String("<svg"), Qt::CaseInsensitive);
}

bool HtmlDocument::isCompleteDocument(const QString &html)
{
    const QStringView t = ltrimmed(html);
    return t.startsWith(QLatin1String("<!doctype"), Qt::CaseInsensitive)
        || t.startsWith(QLatin1String("<html"), Qt::CaseInsensitive);
}

QString HtmlDocument::complete(const QString &content, const QString &mime, const QString &title)
{
    const QString pageTitle = title.trimmed().isEmpty() ? QStringLiteral("Artifact") : title.trimmed();
    const QString safeTitle = escapeAttr(pageTitle);

    if (isSvgType(mime) || looksLikeSvg(content))
    {
        if (isCompleteDocument(content))
            return content;
        return wrapDocument(safeTitle,
                            QStringLiteral("html,body{margin:0;min-height:100%;background:#111}"
                                           "svg{display:block;max-width:100%;height:auto;margin:0 auto}"),
                            content);
    }

    if (isCompleteDocument(content))
    {
        if (content.contains(QLatin1String("charset"), Qt::CaseInsensitive))
            return content;
        QString out = content;
        const QRegularExpression headRe(QStringLiteral("<head\\b[^>]*>"),
                                        QRegularExpression::CaseInsensitiveOption);
        const auto m = headRe.match(out);
        if (m.hasMatch())
            out.insert(m.capturedEnd(), QStringLiteral("\n<meta charset=\"utf-8\">"));
        return out;
    }

    return wrapDocument(safeTitle,
                        QStringLiteral("\n"
                                       "  html { color-scheme: dark; }\n"
                                       "  body { margin: 0; padding: 20px; font-family: system-ui, sans-serif;\n"
                                       "         line-height: 1.45; color: #ececec; background: #212121; }\n"
                                       "  a { color: #93c5fd; }\n"
                                       "  table { border-collapse: collapse; }\n"
                                       "  th, td { border: 1px solid #424242; padding: 6px 10px; }\n"
                                       "  img, canvas, svg, video, iframe { max-width: 100%; }\n"),
                        content);
}
