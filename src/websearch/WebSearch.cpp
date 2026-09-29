#include "websearch/WebSearch.h"
#include "artifacts/Attach.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
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
    , m_policy(&WebSearch::addressAllowed)
{
    m_resolver = [this](const QString &host, const ResolveDone &done)
    {
        QHostInfo::lookupHost(host, this, [done](const QHostInfo &info)
        {
            if (info.error() != QHostInfo::NoError)
                done({}, info.errorString());
            else
                done(info.addresses(), {});
        });
    };
}

WebSearch::~WebSearch()
{
    // Stop in-flight fetches while the network manager still exists. Their
    // callbacks are not run: whatever they belong to is going away too.
    qDeleteAll(m_fetches);
    m_fetches.clear();
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
                             "fetch JavaScript/CSS libraries or CDN bundles — link them with <script src> or "
                             "<link>. Do not claim you cannot browse. For open-ended questions without a URL, "
                             "use web_search."));
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

namespace {

// IPv4 ranges that are not the public internet.
bool ipv4Allowed(quint32 v)
{
    struct Block
    {
        quint32 net;
        int bits;
    };
    static const Block blocked[] = {
        {0x00000000, 8},  // 0.0.0.0/8       "this network"
        {0x0a000000, 8},  // 10.0.0.0/8      private
        {0x64400000, 10}, // 100.64.0.0/10   carrier-grade NAT
        {0x7f000000, 8},  // 127.0.0.0/8     loopback
        {0xa9fe0000, 16}, // 169.254.0.0/16  link-local, incl. cloud metadata
        {0xac100000, 12}, // 172.16.0.0/12   private
        {0xc0000000, 24}, // 192.0.0.0/24    IETF protocol assignments
        {0xc0000200, 24}, // 192.0.2.0/24    documentation
        {0xc0a80000, 16}, // 192.168.0.0/16  private
        {0xc6120000, 15}, // 198.18.0.0/15   benchmarking
        {0xc6336400, 24}, // 198.51.100.0/24 documentation
        {0xcb007100, 24}, // 203.0.113.0/24  documentation
        {0xe0000000, 4},  // 224.0.0.0/4     multicast
        {0xf0000000, 4},  // 240.0.0.0/4     reserved, incl. broadcast
    };
    for (const Block &blk : blocked)
    {
        const quint32 mask = ~quint32(0) << (32 - blk.bits);
        if ((v & mask) == blk.net)
            return false;
    }
    return true;
}

// Hostnames that are never public, and single-label names, which the
// operating system resolves through the local network's search domains.
// Anything else is resolved and its addresses checked before it is requested.
bool hostNameAllowed(const QString &host)
{
    if (host.isEmpty() || !host.contains(QLatin1Char('.')))
        return false;
    static const QStringList reserved = {
        QStringLiteral(".localhost"),   QStringLiteral(".local"),    QStringLiteral(".localdomain"),
        QStringLiteral(".internal"),    QStringLiteral(".lan"),      QStringLiteral(".home.arpa"),
        QStringLiteral(".intranet"),
    };
    for (const QString &suffix : reserved)
    {
        if (host.endsWith(suffix))
            return false;
    }
    return true;
}

QString normalizedHost(const QUrl &url)
{
    QString host = url.host().toLower();
    while (host.endsWith(QLatin1Char('.')))
        host.chop(1);
    return host;
}

bool urlAllowedWith(const QUrl &url, const WebSearch::AddressPolicy &policy)
{
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;
    const QString host = normalizedHost(url);
    if (host.isEmpty())
        return false;
    const QHostAddress literal(host);
    if (!literal.isNull())
        return policy(literal);
    return hostNameAllowed(host);
}

} // namespace

bool WebSearch::addressAllowed(const QHostAddress &addr)
{
    if (addr.isNull())
        return false;
    if (addr.protocol() == QAbstractSocket::IPv4Protocol)
        return ipv4Allowed(addr.toIPv4Address());
    if (addr.protocol() != QAbstractSocket::IPv6Protocol)
        return false;

    const Q_IPV6ADDR a = addr.toIPv6Address();
    const auto v4At = [&a](int off)
    {
        return (quint32(a[off]) << 24) | (quint32(a[off + 1]) << 16) | (quint32(a[off + 2]) << 8)
            | quint32(a[off + 3]);
    };
    const auto zeros = [&a](int from, int to)
    {
        for (int i = from; i < to; ++i)
        {
            if (a[i])
                return false;
        }
        return true;
    };
    if (zeros(0, 12))                                            // ::/96  unspecified, loopback, IPv4-compatible
        return false;
    if (zeros(0, 10) && a[10] == 0xff && a[11] == 0xff)         // ::ffff:0:0/96  IPv4-mapped
        return ipv4Allowed(v4At(12));
    if (a[0] == 0x00 && a[1] == 0x64 && a[2] == 0xff && a[3] == 0x9b && zeros(4, 12)) // 64:ff9b::/96  NAT64
        return ipv4Allowed(v4At(12));
    if (a[0] == 0x20 && a[1] == 0x02)                           // 2002::/16  6to4
        return ipv4Allowed(v4At(2));
    if (a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x00 && a[3] == 0x00) // 2001::/32  Teredo
        return false;
    if (a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0d && a[3] == 0xb8) // 2001:db8::/32  documentation
        return false;
    if ((a[0] & 0xfe) == 0xfc)                                  // fc00::/7  unique local
        return false;
    if (a[0] == 0xfe)                                           // fe00::/8  link-local, site-local, reserved
        return false;
    if (a[0] == 0xff)                                           // ff00::/8  multicast
        return false;
    if (a[0] == 0x01 && a[1] == 0x00 && zeros(2, 8))            // 100::/64  discard-only
        return false;
    return true;
}

bool WebSearch::urlAllowed(const QUrl &url)
{
    return urlAllowedWith(url, &WebSearch::addressAllowed);
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

namespace {

constexpr int kMaxFetchChars = 24000;

bool isStaticAssetContentType(const QString &contentType)
{
    QString ct = contentType;
    const int semi = ct.indexOf(QLatin1Char(';'));
    if (semi >= 0)
    {
        ct = ct.left(semi);
    }
    ct = ct.trimmed().toLower();
    return ct.contains(QLatin1String("javascript")) || ct.contains(QLatin1String("ecmascript"))
        || ct == QLatin1String("text/css") || ct.contains(QLatin1String("wasm"))
        || ct.startsWith(QLatin1String("font/"));
}

// Types that are not a text document. Known from the response headers, so a
// download of one can be stopped before any of the body is read.
bool isNonTextContentType(const QString &contentType)
{
    QString ct = contentType;
    const int semi = ct.indexOf(QLatin1Char(';'));
    if (semi >= 0)
        ct = ct.left(semi);
    ct = ct.trimmed().toLower();
    return ct.startsWith(QLatin1String("image/")) || ct.startsWith(QLatin1String("audio/"))
        || ct.startsWith(QLatin1String("video/")) || isStaticAssetContentType(contentType)
        || ct == QLatin1String("application/pdf") || ct == QLatin1String("application/octet-stream")
        || ct == QLatin1String("application/zip") || ct == QLatin1String("application/gzip");
}

} // namespace

bool WebSearch::isStaticAssetUrl(const QUrl &url)
{
    const QString path = url.path().toLower();
    static const QStringList ext{
        QStringLiteral(".js"),   QStringLiteral(".mjs"),  QStringLiteral(".cjs"),
        QStringLiteral(".css"),  QStringLiteral(".map"),  QStringLiteral(".wasm"),
        QStringLiteral(".woff"), QStringLiteral(".woff2"), QStringLiteral(".ttf"),
        QStringLiteral(".eot"),  QStringLiteral(".otf"),
    };
    for (const QString &e : ext)
    {
        if (path.endsWith(e))
        {
            return true;
        }
    }
    return false;
}

QString WebSearch::staticAssetHint(const QUrl &url)
{
    const QString href = url.toString();
    const QString path = url.path().toLower();
    if (path.endsWith(QLatin1String(".css")))
    {
        return QStringLiteral(
                   "URL: %1\n\nThis is a CSS stylesheet, not a document. Do not inline it. "
                   "In HTML use:\n<link rel=\"stylesheet\" href=\"%1\">")
            .arg(href);
    }
    return QStringLiteral(
               "URL: %1\n\nThis is a JavaScript/CSS library, not a document. Do not inline it "
               "(it would fill the context window). In HTML use:\n<script src=\"%1\"></script>")
        .arg(href);
}

static QUrl urlFromFetchedText(const QString &content)
{
    const int at = content.indexOf(QStringLiteral("URL: "));
    if (at < 0)
    {
        return {};
    }
    const int start = at + 5;
    int end = content.indexOf(QLatin1Char('\n'), start);
    if (end < 0)
    {
        end = content.size();
    }
    return QUrl(content.mid(start, end - start).trimmed());
}

QString WebSearch::clipForModel(const QString &content)
{
    const QUrl u = urlFromFetchedText(content);
    if (u.isValid() && isStaticAssetUrl(u))
    {
        return staticAssetHint(u);
    }
    if (content.size() <= kMaxFetchChars)
    {
        return content;
    }
    QString t = content.left(kMaxFetchChars);
    t += QStringLiteral("\n\n[truncated]");
    return t;
}

QString WebSearch::extractText(const QByteArray &body, const QString &contentType)
{
    QString ct = contentType;
    const int semi = ct.indexOf(QLatin1Char(';'));
    if (semi >= 0)
        ct = ct.left(semi);
    ct = ct.trimmed().toLower();
    if (isNonTextContentType(contentType))
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
    m_searchReplies.insert(reply);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, p, cb]()
    {
        m_searchReplies.remove(reply);
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

// One page fetch. It reads the body as it arrives and stops the moment carrying
// on cannot help (see WebSearch::fetch). Owned by the WebSearch that made it and
// gone once it has reported its result.
class WebSearch::Fetch : public QObject
{
public:
    Fetch(WebSearch *ws, const QUrl &url, const WebFetchOptions &opts, Callback cb)
        : QObject(ws)
        , m_ws(ws)
        , m_url(url)
        , m_opts(opts)
        , m_cb(std::move(cb))
    {
        m_stall.setSingleShot(true);
        m_total.setSingleShot(true);
        connect(&m_stall, &QTimer::timeout, this, [this]()
        {
            fail(QStringLiteral("no data received for %1 s").arg(m_opts.stallMs / 1000.0, 0, 'g', 3));
        });
        connect(&m_total, &QTimer::timeout, this, [this]()
        {
            fail(QStringLiteral("gave up after %1 s").arg(m_opts.totalMs / 1000.0, 0, 'g', 3));
        });
    }

    ~Fetch() override { dropReply(); }

    void start()
    {
        m_total.start(m_opts.totalMs);
        begin(m_url);
    }

    void cancel() { fail(QStringLiteral("cancelled")); }

private:
    static constexpr int kMaxRedirects = 5;
    static constexpr qint64 kSniffBytes = 4096;
    static constexpr qint64 kMaxRedirectBody = 64 * 1024;
    static constexpr qint64 kReadBuffer = 256 * 1024;

    static bool isRedirect(int status)
    {
        return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    }

    QString notAllowed() const
    {
        return m_hops > 0 ? QStringLiteral("the page redirected to a URL that is not allowed")
                          : QStringLiteral("that URL is not allowed");
    }

    // Checks that need no network, then a lookup of the hostname: every address
    // it resolves to must be acceptable, not just the first.
    void begin(const QUrl &url)
    {
        m_url = url;
        if (!urlAllowedWith(url, m_ws->m_policy))
        {
            fail(notAllowed());
            return;
        }
        const QString host = normalizedHost(url);
        if (!QHostAddress(host).isNull())
        {
            request();
            return;
        }
        QPointer<Fetch> self(this);
        m_ws->m_resolver(host, [self](QList<QHostAddress> addresses, QString error)
        {
            if (self)
                self->resolved(addresses, error);
        });
    }

    void resolved(const QList<QHostAddress> &addresses, const QString &error)
    {
        if (m_done)
            return;
        if (!error.isEmpty() || addresses.isEmpty())
        {
            fail(QStringLiteral("could not look up %1").arg(m_url.host()));
            return;
        }
        for (const QHostAddress &a : addresses)
        {
            if (!m_ws->m_policy(a))
            {
                fail(notAllowed());
                return;
            }
        }
        request();
    }

    void request()
    {
        QNetworkRequest req(m_url);
        // Redirects are followed by hand so each hop gets the same checks.
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setRawHeader("User-Agent", "Shammy/0.1");
        req.setRawHeader("Accept",
                         "text/html, text/plain, text/markdown, application/json, "
                         "application/xhtml+xml, */*;q=0.8");
        m_body.clear();
        m_discarded = 0;
        m_status = 0;
        m_headersSeen = false;
        m_sniffed = false;
        m_contentType.clear();
        m_reply = m_ws->m_nam.get(req);
        m_reply->setReadBufferSize(kReadBuffer);
        connect(m_reply, &QNetworkReply::metaDataChanged, this, [this]() { onMeta(); });
        connect(m_reply, &QNetworkReply::readyRead, this, [this]() { onData(); });
        connect(m_reply, &QNetworkReply::downloadProgress, this, [this]() { kick(); });
        connect(m_reply, &QNetworkReply::finished, this, [this]() { onFinished(); });
        kick();
    }

    // Any sign of life counts as progress and restarts the stall timer.
    void kick()
    {
        if (!m_done)
            m_stall.start(m_opts.stallMs);
    }

    void onMeta()
    {
        kick();
        if (m_headersSeen || !m_reply)
            return;
        const QVariant status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (!status.isValid())
            return;
        m_headersSeen = true;
        m_status = status.toInt();
        m_contentType = m_reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (isRedirect(m_status))
            return; // followed once the (small) reply is complete
        if (m_status >= 400)
        {
            fail(httpError(), true);
            return;
        }
        // The type is known before any of the body is: refuse what cannot help.
        if (isStaticAssetContentType(m_contentType))
        {
            hintAndStop();
            return;
        }
        if (isNonTextContentType(m_contentType))
            fail(notText());
    }

    void onData()
    {
        if (m_done || !m_reply)
            return;
        if (!m_headersSeen)
            onMeta();
        if (m_done || !m_reply)
            return;
        kick();
        const QByteArray chunk = m_reply->readAll();
        if (isRedirect(m_status))
        {
            m_discarded += chunk.size();
            if (m_discarded > kMaxRedirectBody)
                fail(QStringLiteral("the redirect response was too large"));
            return;
        }
        m_body += chunk;
        // A page that says it is text but is not: judge it from its first bytes.
        if (!m_sniffed && m_body.size() >= kSniffBytes)
        {
            m_sniffed = true;
            if (!Attach::looksLikeText(m_body.left(kSniffBytes)))
            {
                fail(QStringLiteral("that URL is not a text document (it looks like binary data)"));
                return;
            }
        }
        if (m_body.size() > m_opts.maxBytes)
        {
            m_body.truncate(m_opts.maxBytes);
            dropReply();
            complete(true);
        }
    }

    void onFinished()
    {
        if (m_done || !m_reply)
            return;
        m_stall.stop();
        if (isRedirect(m_status))
        {
            followRedirect();
            return;
        }
        if (m_status >= 400)
        {
            fail(httpError());
            return;
        }
        if (m_reply->error() != QNetworkReply::NoError)
        {
            fail(m_reply->errorString());
            return;
        }
        m_body += m_reply->readAll();
        bool clipped = false;
        if (m_body.size() > m_opts.maxBytes)
        {
            m_body.truncate(m_opts.maxBytes);
            clipped = true;
        }
        if (!m_sniffed && !Attach::looksLikeText(m_body.left(kSniffBytes)))
        {
            fail(QStringLiteral("that URL is not a text document (it looks like binary data)"));
            return;
        }
        complete(clipped);
    }

    void followRedirect()
    {
        // Read the raw header: Qt's parsed LocationHeader is empty for the very
        // common relative form ("Location: /next").
        const QByteArray rawLocation = m_reply->rawHeader("Location").trimmed();
        const QUrl location = QUrl::fromEncoded(rawLocation, QUrl::TolerantMode);
        if (rawLocation.isEmpty() || !location.isValid())
        {
            fail(QStringLiteral("the page redirected without saying where"));
            return;
        }
        if (++m_hops > kMaxRedirects)
        {
            fail(QStringLiteral("too many redirects"));
            return;
        }
        const QUrl next = canonicalizeFetchUrl(m_url.resolved(location));
        if (m_url.scheme().toLower() == QLatin1String("https") && next.scheme().toLower() != QLatin1String("https"))
        {
            fail(QStringLiteral("the page redirected from https to http"));
            return;
        }
        dropReply();
        if (isStaticAssetUrl(next))
        {
            finish(staticAssetHint(next), {});
            return;
        }
        begin(next);
    }

    void complete(bool clipped)
    {
        QString text = extractText(m_body, m_contentType);
        if (text.trimmed().isEmpty())
        {
            finish({}, notText());
            return;
        }
        if (text.size() > kMaxFetchChars)
        {
            text.truncate(kMaxFetchChars);
            text += QStringLiteral("\n\n[truncated]");
        }
        else if (clipped)
        {
            text += QStringLiteral("\n\n[truncated]");
        }
        finish(QStringLiteral("URL: %1\n\n%2").arg(m_url.toString(), text), {});
    }

    QString notText() const
    {
        return QStringLiteral("that URL is not a text document (%1)")
            .arg(m_contentType.isEmpty() ? QStringLiteral("unknown type") : m_contentType);
    }

    QString httpError() const
    {
        const QString reason =
            m_reply ? m_reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString() : QString();
        return QStringLiteral("HTTP %1 %2").arg(m_status).arg(reason).trimmed();
    }

    void hintAndStop()
    {
        const QString hint = staticAssetHint(m_url);
        dropReply();
        finish(hint, {});
    }

    // Stops the transfer at once, which is what keeps a huge body from being
    // pulled in. The exception is an HTTP error status: Qt reports its own error
    // for those while it is still working through the response it just
    // delivered, and an abort on top of that makes it log "error must only be
    // called once". So for those the abort waits for the next turn of the event
    // loop, by which time a short error page has finished by itself.
    void dropReply(bool afterQtSettles = false)
    {
        if (!m_reply)
            return;
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        if (afterQtSettles)
        {
            QMetaObject::invokeMethod(
                reply,
                [reply]()
                {
                    if (reply->isRunning())
                        reply->abort();
                    reply->deleteLater();
                },
                Qt::QueuedConnection);
            return;
        }
        if (reply->isRunning())
            reply->abort();
        reply->deleteLater();
    }

    void fail(const QString &error, bool afterQtSettles = false)
    {
        dropReply(afterQtSettles);
        finish({}, error);
    }

    void finish(const QString &text, const QString &error)
    {
        if (m_done)
            return;
        m_done = true;
        m_stall.stop();
        m_total.stop();
        m_ws->m_fetches.removeAll(this);
        Callback cb = std::move(m_cb);
        deleteLater();
        if (cb)
            cb(text, error);
    }

    WebSearch *m_ws;
    QUrl m_url;
    WebFetchOptions m_opts;
    Callback m_cb;
    QPointer<QNetworkReply> m_reply;
    QTimer m_stall;
    QTimer m_total;
    QByteArray m_body;
    QString m_contentType;
    qint64 m_discarded = 0;
    int m_status = 0;
    int m_hops = 0;
    bool m_headersSeen = false;
    bool m_sniffed = false;
    bool m_done = false;
};

void WebSearch::fetch(const QString &urlStr, const Callback &cb)
{
    fetch(urlStr, cb, WebFetchOptions());
}

void WebSearch::fetch(const QString &urlStr, const Callback &cb, const WebFetchOptions &requested)
{
    QUrl url = QUrl::fromUserInput(urlStr.trimmed());
    if (!url.isValid() || url.host().isEmpty())
    {
        cb({}, QStringLiteral("invalid URL"));
        return;
    }
    url = canonicalizeFetchUrl(url);
    if (!urlAllowedWith(url, m_policy))
    {
        cb({}, QStringLiteral("that URL is not allowed"));
        return;
    }
    if (isStaticAssetUrl(url))
    {
        cb(staticAssetHint(url), {});
        return;
    }
    WebFetchOptions opts = requested;
    if (opts.stallMs <= 0)
        opts.stallMs = WebFetchOptions().stallMs;
    if (opts.totalMs < opts.stallMs)
        opts.totalMs = opts.stallMs;
    if (opts.maxBytes <= 0)
        opts.maxBytes = WebFetchOptions::kDefaultMaxBytes;
    auto *job = new Fetch(this, url, opts, cb);
    m_fetches.append(job);
    job->start();
}

void WebSearch::cancelAll()
{
    const QList<Fetch *> fetches = m_fetches;
    for (Fetch *f : fetches)
        f->cancel();
    const QSet<QNetworkReply *> replies = m_searchReplies;
    for (QNetworkReply *r : replies)
        r->abort();
}
