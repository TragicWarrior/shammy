#pragma once

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

// Limits for one web_fetch. The defaults are what the app uses; the two
// timeouts are user settings (Settings > General and Advanced).
struct WebFetchOptions
{
    // Raw bytes of a page that are read before the download is cut off. This is
    // the amount of markup parsed, not what the model sees: the extracted text
    // is trimmed separately (WebSearch.cpp: kMaxFetchChars). It is well above
    // that so that a page with a lot of script and style ahead of its text
    // still yields the text, while staying small enough to be cheap.
    static constexpr qint64 kDefaultMaxBytes = 1024 * 1024;

    // A fetch is abandoned when no data at all arrives for this long. The timer
    // restarts whenever any more data arrives, so a slow but steady download is
    // not cut off.
    static constexpr int kDefaultStallSeconds = 30;
    static constexpr int kMinStallSeconds = 5;
    static constexpr int kMaxStallSeconds = 300;

    // Backstop on the whole fetch (lookup, redirects and download), so a server
    // that trickles data can never hold a tool round open indefinitely.
    static constexpr int kDefaultTotalSeconds = 300;
    static constexpr int kMinTotalSeconds = 30;
    static constexpr int kMaxTotalSeconds = 3600;

    int stallMs = kDefaultStallSeconds * 1000;
    int totalMs = kDefaultTotalSeconds * 1000;
    qint64 maxBytes = kDefaultMaxBytes;
};

class WebSearch : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(QString text, QString error)>;
    // Decides whether a resolved or literal address may be contacted.
    using AddressPolicy = std::function<bool(const QHostAddress &)>;
    // Asynchronous hostname lookup: calls done(addresses, error) exactly once.
    using ResolveDone = std::function<void(QList<QHostAddress> addresses, QString error)>;
    using Resolver = std::function<void(const QString &host, const ResolveDone &done)>;

    explicit WebSearch(QObject *parent = nullptr);
    ~WebSearch() override;

    static QJsonObject toolDefinition();
    static QJsonObject fetchToolDefinition();
    static QString formatBrave(const QJsonObject &body);
    static QString formatTavily(const QJsonObject &body);
    static QString formatExa(const QJsonObject &body);
    // Whether an address is one a page fetch may contact: false for loopback,
    // private, link-local, multicast, reserved and other non-public ranges,
    // including IPv4 addresses carried inside IPv6 (mapped, NAT64, 6to4).
    static bool addressAllowed(const QHostAddress &addr);
    // Checks that can be made without a network lookup: scheme, hostname, and
    // a literal address. A hostname that passes here is still resolved and its
    // addresses checked before anything is requested.
    static bool urlAllowed(const QUrl &url);
    static QUrl canonicalizeFetchUrl(const QUrl &url);
    static QString extractText(const QByteArray &body, const QString &contentType);
    static bool isStaticAssetUrl(const QUrl &url);
    static QString staticAssetHint(const QUrl &url);
    // Shrink a stored tool payload before it is sent back to the model.
    static QString clipForModel(const QString &content);

    void search(const QString &provider, const QString &apiKey, const QString &query,
                const Callback &cb);
    // Fetches a page as text. The download is read as it arrives and stopped as
    // soon as continuing cannot help: a non-text type, binary data, an HTTP
    // error, the size cap, a stall, or the overall time limit. Redirects are
    // followed by hand so that every hop is checked like the first request.
    // cb is called exactly once.
    void fetch(const QString &url, const Callback &cb);
    void fetch(const QString &url, const Callback &cb, const WebFetchOptions &opts);
    // Aborts every fetch and search in flight; each callback still runs once,
    // with an error.
    void cancelAll();

    // Test seams. The defaults are addressAllowed() and a QHostInfo lookup.
    void setAddressPolicy(AddressPolicy policy) { m_policy = std::move(policy); }
    void setResolver(Resolver resolver) { m_resolver = std::move(resolver); }

private:
    class Fetch;
    friend class Fetch;

    QNetworkAccessManager m_nam;
    AddressPolicy m_policy;
    Resolver m_resolver;
    QList<Fetch *> m_fetches;
    QSet<QNetworkReply *> m_searchReplies;
};
