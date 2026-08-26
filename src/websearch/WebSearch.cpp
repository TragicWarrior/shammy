#include "websearch/WebSearch.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

static QString clip(QString s, int max = 400)
{
    s = s.simplified();
    if (s.size() > max)
    {
        s.truncate(max - 1);
        s += QChar(0x2026);
    }
    return s;
}

static QString formatHits(const QJsonArray &hits)
{
    QStringList lines;
    int n = 0;
    for (const QJsonValue &v : hits)
    {
        const QJsonObject o = v.toObject();
        const QString title = o.value(QStringLiteral("title")).toString();
        const QString url = o.value(QStringLiteral("url")).toString();
        const QString snip = o.value(QStringLiteral("snippet")).toString();
        if (title.isEmpty() && url.isEmpty())
            continue;
        ++n;
        lines << QStringLiteral("%1. %2").arg(n).arg(title.isEmpty() ? url : title);
        if (!url.isEmpty())
            lines << QStringLiteral("   %1").arg(url);
        if (!snip.isEmpty())
            lines << QStringLiteral("   %1").arg(snip);
    }
    if (lines.isEmpty())
        return QStringLiteral("No results.");
    return lines.join(QLatin1Char('\n'));
}

WebSearch::WebSearch(QObject *parent)
    : QObject(parent)
{
}

QJsonObject WebSearch::toolDefinition()
{
    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("object"));
    params.insert(QStringLiteral("properties"),
                  QJsonObject{{QStringLiteral("query"),
                               QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                           {QStringLiteral("description"),
                                            QStringLiteral("Search query")}}}});
    params.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("query")});
    QJsonObject fn;
    fn.insert(QStringLiteral("name"), QStringLiteral("web_search"));
    fn.insert(QStringLiteral("description"),
              QStringLiteral("Search the live web. Use for current events, facts that may have changed, "
                             "or anything not in your training data."));
    fn.insert(QStringLiteral("parameters"), params);
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                       {QStringLiteral("function"), fn}};
}

QJsonObject WebSearch::fetchToolDefinition()
{
    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("object"));
    params.insert(QStringLiteral("properties"),
                  QJsonObject{{QStringLiteral("url"),
                               QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                           {QStringLiteral("description"),
                                            QStringLiteral("http(s) URL to fetch (page, README, docs, raw file)")}}}});
    params.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("url")});
    QJsonObject fn;
    fn.insert(QStringLiteral("name"), QStringLiteral("web_fetch"));
    fn.insert(QStringLiteral("description"),
              QStringLiteral("Fetch a URL and return its text. Use when the user gives a link, or when you "
                             "need the contents of a specific page (GitHub README, docs, article). Do not "
                             "claim you cannot browse. For open-ended questions without a URL, use web_search."));
    fn.insert(QStringLiteral("parameters"), params);
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                       {QStringLiteral("function"), fn}};
}

static QString decodeEntities(QString s)
{
    s.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    s.replace(QLatin1String("&amp;"), QLatin1String("&"));
    s.replace(QLatin1String("&lt;"), QLatin1String("<"));
    s.replace(QLatin1String("&gt;"), QLatin1String(">"));
    s.replace(QLatin1String("&quot;"), QLatin1String("\""));
    s.replace(QLatin1String("&#39;"), QLatin1String("'"));
    s.replace(QLatin1String("&apos;"), QLatin1String("'"));
    QRegularExpression num(QStringLiteral("&#(\\d{1,7});"));
    QRegularExpressionMatchIterator it = num.globalMatch(s);
    QString out;
    int last = 0;
    while (it.hasNext())
    {
        const QRegularExpressionMatch m = it.next();
        out += QStringView{s}.mid(last, m.capturedStart() - last);
        out += QChar(m.captured(1).toInt());
        last = m.capturedEnd();
    }
    out += QStringView{s}.mid(last);
    return out;
}

static QString htmlToText(QString html)
{
    const auto ci = QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption;
    html.replace(QRegularExpression(QStringLiteral("<script\\b[^>]*>.*?</script>"), ci), QString());
    html.replace(QRegularExpression(QStringLiteral("<style\\b[^>]*>.*?</style>"), ci), QString());
    html.replace(QRegularExpression(QStringLiteral("<noscript\\b[^>]*>.*?</noscript>"), ci), QString());
    html.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption),
                 QLatin1String("\n"));
    html.replace(QRegularExpression(QStringLiteral("</(p|div|h[1-6]|li|tr|table|section|article|blockquote)>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QLatin1String("\n"));
    html.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QString());
    html = decodeEntities(html);
    const QStringList lines = html.split(QLatin1Char('\n'));
    QStringList kept;
    for (QString line : lines)
    {
        line.replace(QRegularExpression(QStringLiteral("[ \\t\\r\\f]+")), QLatin1String(" "));
        line = line.trimmed();
        if (line.isEmpty())
        {
            if (!kept.isEmpty() && !kept.last().isEmpty())
                kept.append(QString());
        }
        else
            kept.append(line);
    }
    while (!kept.isEmpty() && kept.last().isEmpty())
        kept.removeLast();
    return kept.join(QLatin1Char('\n'));
}

bool WebSearch::urlAllowed(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;
    const QString host = url.host().toLower();
    if (host.isEmpty() || host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
        || host == QLatin1String("::1") || host == QLatin1String("[::1]") || host == QLatin1String("0.0.0.0")
        || host == QLatin1String("metadata.google.internal") || host.endsWith(QLatin1String(".localhost"))
        || host.endsWith(QLatin1String(".local")))
        return false;
    const QHostAddress addr(host);
    if (!addr.isNull())
    {
        if (addr.isLoopback() || addr.isLinkLocal() || addr.isMulticast())
            return false;
        if (addr.protocol() == QAbstractSocket::IPv4Protocol)
        {
            const quint32 v = addr.toIPv4Address();
            if ((v & 0xff000000u) == 0x0a000000u     // 10.0.0.0/8
                || (v & 0xfff00000u) == 0xac100000u  // 172.16.0.0/12
                || (v & 0xffff0000u) == 0xc0a80000u  // 192.168.0.0/16
                || (v & 0xffc00000u) == 0x64400000u  // 100.64.0.0/10
                || (v & 0xff000000u) == 0x7f000000u) // 127.0.0.0/8
                return false;
        }
    }
    return true;
}

QUrl WebSearch::canonicalizeFetchUrl(const QUrl &url)
{
    const QString host = url.host().toLower();
    if (host != QLatin1String("github.com") && host != QLatin1String("www.github.com"))
        return url;
    const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return url;
    const QString owner = parts.at(0);
    const QString repo = parts.at(1);
    QString rawPath;
    if (parts.size() == 2)
        rawPath = QLatin1Char('/') + owner + QLatin1Char('/') + repo + QStringLiteral("/HEAD/README.md");
    else if (parts.size() >= 4 && parts.at(2) == QLatin1String("blob"))
    {
        rawPath = QLatin1Char('/') + owner + QLatin1Char('/') + repo + QLatin1Char('/') + parts.at(3);
        for (int i = 4; i < parts.size(); ++i)
            rawPath += QLatin1Char('/') + parts.at(i);
    }
    else if (parts.size() >= 3 && parts.at(2) == QLatin1String("tree"))
    {
        const QString ref = parts.size() >= 4 ? parts.at(3) : QStringLiteral("HEAD");
        rawPath = QLatin1Char('/') + owner + QLatin1Char('/') + repo + QLatin1Char('/') + ref
            + QStringLiteral("/README.md");
    }
    else
        return url;
    QUrl raw;
    raw.setScheme(QStringLiteral("https"));
    raw.setHost(QStringLiteral("raw.githubusercontent.com"));
    raw.setPath(rawPath);
    return raw;
}

QString WebSearch::extractText(const QByteArray &body, const QString &contentType)
{
    QString ct = contentType;
    const int semi = ct.indexOf(QLatin1Char(';'));
    if (semi >= 0)
        ct = ct.left(semi);
    ct = ct.trimmed().toLower();
    if (ct.startsWith(QLatin1String("image/")) || ct.startsWith(QLatin1String("audio/"))
        || ct.startsWith(QLatin1String("video/")) || ct == QLatin1String("application/pdf")
        || ct == QLatin1String("application/octet-stream") || ct == QLatin1String("application/zip")
        || ct == QLatin1String("application/gzip"))
        return {};
    const QString s = QString::fromUtf8(body);
    const bool html = ct.contains(QLatin1String("html"))
        || s.startsWith(QLatin1String("<!DOCTYPE html"), Qt::CaseInsensitive)
        || s.startsWith(QLatin1String("<html"), Qt::CaseInsensitive);
    if (html)
        return htmlToText(s);
    return s;
}

QString WebSearch::formatBrave(const QJsonObject &body)
{
    QJsonArray hits;
    const QJsonArray raw = body.value(QStringLiteral("web")).toObject().value(QStringLiteral("results")).toArray();
    for (const QJsonValue &v : raw)
    {
        const QJsonObject o = v.toObject();
        hits.append(QJsonObject{
            {QStringLiteral("title"), o.value(QStringLiteral("title")).toString()},
            {QStringLiteral("url"), o.value(QStringLiteral("url")).toString()},
            {QStringLiteral("snippet"),
             o.value(QStringLiteral("description")).toString()},
        });
    }
    return formatHits(hits);
}

QString WebSearch::formatTavily(const QJsonObject &body)
{
    QJsonArray hits;
    const QJsonArray raw = body.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &v : raw)
    {
        const QJsonObject o = v.toObject();
        hits.append(QJsonObject{
            {QStringLiteral("title"), o.value(QStringLiteral("title")).toString()},
            {QStringLiteral("url"), o.value(QStringLiteral("url")).toString()},
            {QStringLiteral("snippet"), o.value(QStringLiteral("content")).toString()},
        });
    }
    QString out = formatHits(hits);
    const QString answer = body.value(QStringLiteral("answer")).toString();
    if (!answer.isEmpty())
        out = QStringLiteral("Summary: %1\n\n%2").arg(clip(answer, 800), out);
    return out;
}

QString WebSearch::formatExa(const QJsonObject &body)
{
    QJsonArray hits;
    const QJsonArray raw = body.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &v : raw)
    {
        const QJsonObject o = v.toObject();
        QString snip = o.value(QStringLiteral("text")).toString();
        if (snip.isEmpty())
        {
            const QJsonArray hl = o.value(QStringLiteral("highlights")).toArray();
            QStringList bits;
            for (const QJsonValue &h : hl)
                bits << h.toString();
            snip = bits.join(QStringLiteral(" … "));
        }
        hits.append(QJsonObject{
            {QStringLiteral("title"), o.value(QStringLiteral("title")).toString()},
            {QStringLiteral("url"), o.value(QStringLiteral("url")).toString()},
            {QStringLiteral("snippet"), clip(snip)},
        });
    }
    return formatHits(hits);
}

void WebSearch::search(const QString &provider, const QString &apiKey, const QString &query,
                       const std::function<void(QString text, QString error)> &cb)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
    {
        cb({}, QStringLiteral("empty search query"));
        return;
    }
    const QString key = apiKey.trimmed();
    if (key.isEmpty())
    {
        cb({}, QStringLiteral("web search API key is not set"));
        return;
    }
    const QString p = provider.trimmed().toLower();
    QNetworkRequest req;
    req.setTransferTimeout(20000);
    QNetworkReply *reply = nullptr;
    if (p == QLatin1String("tavily"))
    {
        req.setUrl(QUrl(QStringLiteral("https://api.tavily.com/search")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        const QJsonObject body{
            {QStringLiteral("api_key"), key},
            {QStringLiteral("query"), q},
            {QStringLiteral("search_depth"), QStringLiteral("basic")},
            {QStringLiteral("max_results"), 8},
        };
        reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    else if (p == QLatin1String("exa"))
    {
        req.setUrl(QUrl(QStringLiteral("https://api.exa.ai/search")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
        const QJsonObject body{
            {QStringLiteral("query"), q},
            {QStringLiteral("type"), QStringLiteral("auto")},
            {QStringLiteral("numResults"), 8},
            {QStringLiteral("contents"),
             QJsonObject{{QStringLiteral("text"),
                          QJsonObject{{QStringLiteral("maxCharacters"), 400}}}}},
        };
        reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    else
    {
        QUrl url(QStringLiteral("https://api.search.brave.com/res/v1/web/search"));
        QUrlQuery uq;
        uq.addQueryItem(QStringLiteral("q"), q);
        uq.addQueryItem(QStringLiteral("count"), QStringLiteral("8"));
        url.setQuery(uq);
        req.setUrl(url);
        req.setRawHeader("Accept", "application/json");
        req.setRawHeader("X-Subscription-Token", key.toUtf8());
        reply = m_nam.get(req);
    }
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, p, cb]()
    {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError)
        {
            QString err = reply->errorString();
            const QJsonObject o = QJsonDocument::fromJson(raw).object();
            const QString api = o.value(QStringLiteral("message")).toString();
            if (api.isEmpty() && o.contains(QStringLiteral("error")))
            {
                const QJsonValue e = o.value(QStringLiteral("error"));
                err = e.isString() ? e.toString() : QString::fromUtf8(QJsonDocument(e.toObject()).toJson(QJsonDocument::Compact));
            }
            else if (!api.isEmpty())
                err = api;
            cb({}, err);
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject())
        {
            cb({}, QStringLiteral("search returned non-JSON"));
            return;
        }
        QString text;
        if (p == QLatin1String("tavily"))
            text = formatTavily(doc.object());
        else if (p == QLatin1String("exa"))
            text = formatExa(doc.object());
        else
            text = formatBrave(doc.object());
        cb(text, {});
    });
}

void WebSearch::fetch(const QString &urlStr, const std::function<void(QString text, QString error)> &cb)
{
    QUrl url = QUrl::fromUserInput(urlStr.trimmed());
    if (!url.isValid() || url.host().isEmpty())
    {
        cb({}, QStringLiteral("invalid URL"));
        return;
    }
    url = canonicalizeFetchUrl(url);
    if (!urlAllowed(url))
    {
        cb({}, QStringLiteral("that URL is not allowed"));
        return;
    }

    QNetworkRequest req(url);
    req.setTransferTimeout(20000);
    req.setMaximumRedirectsAllowed(5);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "Shammy/0.1");
    req.setRawHeader("Accept",
                     "text/html, text/plain, text/markdown, application/json, "
                     "application/xhtml+xml, */*;q=0.8");
    QNetworkReply *reply = m_nam.get(req);
    QObject::connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl &next)
    {
        if (!WebSearch::urlAllowed(next))
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, cb]()
    {
        reply->deleteLater();
        constexpr int kMaxBytes = 1024 * 1024;
        constexpr int kMaxChars = 80000;
        if (reply->error() != QNetworkReply::NoError)
        {
            if (reply->error() == QNetworkReply::OperationCanceledError)
                cb({}, QStringLiteral("fetch was redirected to a URL that is not allowed"));
            else
                cb({}, reply->errorString());
            return;
        }
        const QByteArray raw = reply->read(kMaxBytes + 1);
        const bool clippedBytes = raw.size() > kMaxBytes;
        const QByteArray body = clippedBytes ? raw.left(kMaxBytes) : raw;
        const QString ctype = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        QString text = extractText(body, ctype);
        if (text.trimmed().isEmpty())
        {
            cb({}, QStringLiteral("that URL is not a text document (%1)").arg(
                   ctype.isEmpty() ? QStringLiteral("unknown type") : ctype));
            return;
        }
        if (text.size() > kMaxChars)
        {
            text.truncate(kMaxChars);
            text += QStringLiteral("\n\n[truncated]");
        }
        else if (clippedBytes)
            text += QStringLiteral("\n\n[truncated]");
        const QString finalUrl = reply->url().toString();
        cb(QStringLiteral("URL: %1\n\n%2").arg(finalUrl, text), {});
    });
}
