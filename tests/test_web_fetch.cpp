#include "websearch/WebSearch.h"

#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <memory>

// WebSearch::fetch against a local HTTP server that can send huge bodies, stall,
// trickle, redirect and hold connections open. The address policy is relaxed to
// allow loopback so the server is reachable; every other range stays blocked.

namespace
{

class TestServer : public QObject
{
public:
    struct Route
    {
        int status = 200;
        QByteArray reason = "OK";
        QByteArray contentType = "text/html";
        QByteArray location;
        QByteArray body;              // small explicit body, sent with Content-Length
        qint64 streamBytes = 0;       // otherwise: this many bytes of `fill`, close-delimited
        QByteArray fill = "lorem ipsum dolor sit amet ";
        bool endless = false;
        int chunk = 64 * 1024;
        int delayMs = 0;              // > 0: one chunk every delayMs
        bool hold = false;            // headers only, then silence
    };

    TestServer() { connect(&m_server, &QTcpServer::newConnection, this, [this]() { accept(); }); }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }
    QString url(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(path);
    }

    QHash<QString, Route> routes;
    QHash<QString, qint64> sent; // body bytes queued for writing, per path
    QHash<QString, int> hits;

private:
    struct Stream
    {
        Route route;
        QByteArray block;
        qint64 done = 0;
    };

    void accept()
    {
        while (QTcpSocket *s = m_server.nextPendingConnection())
        {
            s->setParent(this);
            connect(s, &QTcpSocket::readyRead, this, [this, s]() { onData(s); });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }

    void onData(QTcpSocket *s)
    {
        QByteArray &buf = m_buf[s];
        buf += s->readAll();
        const int end = buf.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QList<QByteArray> parts = buf.left(buf.indexOf("\r\n")).split(' ');
        buf.clear();
        if (parts.size() >= 2)
            serve(s, QString::fromLatin1(parts.at(1)));
    }

    void serve(QTcpSocket *s, const QString &path)
    {
        ++hits[path];
        Route r;
        if (routes.contains(path))
            r = routes.value(path);
        else
        {
            r.status = 404;
            r.reason = "Not Found";
            r.body = "nope";
        }
        const bool small = !r.body.isEmpty() || (r.streamBytes == 0 && !r.endless && !r.hold);
        QByteArray head = "HTTP/1.1 " + QByteArray::number(r.status) + " " + r.reason + "\r\nConnection: close\r\n";
        if (!r.contentType.isEmpty())
            head += "Content-Type: " + r.contentType + "\r\n";
        if (!r.location.isEmpty())
            head += "Location: " + r.location + "\r\n";
        if (small)
            head += "Content-Length: " + QByteArray::number(r.body.size()) + "\r\n";
        head += "\r\n";
        s->write(head);
        if (small)
        {
            s->write(r.body);
            s->disconnectFromHost();
            return;
        }
        if (r.hold)
            return;
        auto st = std::make_shared<Stream>();
        st->route = r;
        while (st->block.size() < r.chunk)
            st->block += r.fill;
        st->block.truncate(r.chunk);
        pump(s, st, path);
    }

    void pump(QTcpSocket *s, const std::shared_ptr<Stream> &st, const QString &path)
    {
        if (s->state() != QAbstractSocket::ConnectedState)
            return;
        const auto writeChunk = [&]()
        {
            const qint64 n = st->route.endless ? st->block.size()
                                               : qMin<qint64>(st->block.size(), st->route.streamBytes - st->done);
            s->write(st->block.constData(), n);
            st->done += n;
            sent[path] += n;
        };
        const auto finished = [&]() { return !st->route.endless && st->done >= st->route.streamBytes; };
        if (st->route.delayMs > 0)
        {
            writeChunk();
            if (finished())
                s->disconnectFromHost();
            else
                QTimer::singleShot(st->route.delayMs, s, [this, s, st, path]() { pump(s, st, path); });
            return;
        }
        // Keep at most ~2 MB queued so a client that stops reading stops us too.
        while (!finished() && s->bytesToWrite() < 2 * 1024 * 1024)
            writeChunk();
        if (finished())
            s->disconnectFromHost();
        else
            connect(s, &QTcpSocket::bytesWritten, s, [this, s, st, path]() { pump(s, st, path); },
                    Qt::SingleShotConnection);
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buf;
};

struct Outcome
{
    int calls = 0;
    QString text;
    QString error;
    qint64 ms = 0;
};

template <class Pred>
bool waitFor(Pred pred, int ms = 15000)
{
    QElapsedTimer t;
    t.start();
    while (!pred())
    {
        if (t.elapsed() > ms)
            return false;
        QTest::qWait(5);
    }
    return true;
}

WebFetchOptions quick(int stallMs = 3000, int totalMs = 20000)
{
    WebFetchOptions o;
    o.stallMs = stallMs;
    o.totalMs = totalMs;
    return o;
}

Outcome run(WebSearch &ws, const QString &url, const WebFetchOptions &opts = quick())
{
    Outcome out;
    QElapsedTimer t;
    t.start();
    ws.fetch(url,
             [&out, &t](QString text, QString error)
             {
                 ++out.calls;
                 out.text = text;
                 out.error = error;
                 out.ms = t.elapsed();
             },
             opts);
    waitFor([&]() { return out.calls > 0; });
    QTest::qWait(60); // a second call, if there were one, would land here
    return out;
}

// Peak resident memory of this process, in KB (0 if it cannot be read).
long peakKb()
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    for (const QByteArray &line : f.readAll().split('\n'))
    {
        if (line.startsWith("VmHWM:"))
            return line.split(' ').at(line.split(' ').size() - 2).toLong();
    }
    return 0;
}

} // namespace

class TestWebFetch : public QObject
{
    Q_OBJECT
    std::unique_ptr<TestServer> srv;
    std::unique_ptr<WebSearch> ws;

private slots:
    void initTestCase()
    {
        // Some build sandboxes have no loopback interface.
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0))
            QSKIP("needs loopback networking to run a local HTTP server");
    }

    void init()
    {
        srv = std::make_unique<TestServer>();
        QVERIFY(srv->listen());
        ws = std::make_unique<WebSearch>();
        ws->setAddressPolicy([](const QHostAddress &a) { return a.isLoopback() || WebSearch::addressAllowed(a); });
    }
    void cleanup()
    {
        ws.reset();
        srv.reset();
    }

    // -- limits ----------------------------------------------------------------

    void defaultLimits()
    {
        const WebFetchOptions o;
        QCOMPARE(o.maxBytes, qint64(1024 * 1024));
        QCOMPARE(o.stallMs, 30 * 1000);
        QCOMPARE(o.totalMs, 300 * 1000);
    }

    void plainPage()
    {
        srv->routes[QStringLiteral("/p")].body = "<html><body><p>Hello there</p></body></html>";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/p")));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.isEmpty(), qPrintable(o.error));
        QVERIFY(o.text.startsWith(QStringLiteral("URL: http://127.0.0.1:")));
        QVERIFY(o.text.contains(QLatin1String("Hello there")));
        QVERIFY(!o.text.contains(QLatin1String("[truncated]")));
    }

    void missingContentTypeIsTreatedAsText()
    {
        auto &r = srv->routes[QStringLiteral("/nt")];
        r.contentType.clear();
        r.body = "plain words here";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/nt")));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.isEmpty(), qPrintable(o.error));
        QVERIFY(o.text.contains(QLatin1String("plain words here")));
    }

    void hugeResponseIsCutOffAtTheCapWithoutBufferingIt()
    {
        auto &r = srv->routes[QStringLiteral("/big")];
        r.contentType = "text/plain";
        r.streamBytes = qint64(300) * 1024 * 1024;
        const long before = peakKb();
        const Outcome o = run(*ws, srv->url(QStringLiteral("/big")));
        const long after = peakKb();
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.isEmpty(), qPrintable(o.error));
        QVERIFY(o.text.endsWith(QLatin1String("[truncated]")));
        // The 300 MB was never pulled in: the download stopped near the cap.
        QVERIFY2(srv->sent[QStringLiteral("/big")] < 16 * 1024 * 1024,
                 qPrintable(QString::number(srv->sent[QStringLiteral("/big")])));
        QVERIFY2(o.ms < 10000, qPrintable(QString::number(o.ms)));
        if (before > 0)
            QVERIFY2(after - before < 100 * 1024, qPrintable(QStringLiteral("peak grew by %1 KB").arg(after - before)));
    }

    void rawByteCapIsConfigurable()
    {
        auto &r = srv->routes[QStringLiteral("/words")];
        r.contentType = "text/plain";
        r.streamBytes = 200 * 1024;
        WebFetchOptions opts = quick();
        opts.maxBytes = 10 * 1024;
        const Outcome o = run(*ws, srv->url(QStringLiteral("/words")), opts);
        QVERIFY2(o.error.isEmpty(), qPrintable(o.error));
        QVERIFY(o.text.endsWith(QLatin1String("[truncated]")));
        // ~10 KB of page text plus the "URL: ..." header and the marker.
        QVERIFY2(o.text.size() > 9 * 1024 && o.text.size() < 11 * 1024, qPrintable(QString::number(o.text.size())));
    }

    void pagesUnderTheCapAreNotMarkedTruncated()
    {
        auto &r = srv->routes[QStringLiteral("/small")];
        r.contentType = "text/plain";
        r.streamBytes = 5 * 1024;
        const Outcome o = run(*ws, srv->url(QStringLiteral("/small")));
        QVERIFY2(o.error.isEmpty(), qPrintable(o.error));
        QVERIFY(!o.text.contains(QLatin1String("[truncated]")));
    }

    // -- stop early when it cannot help -----------------------------------------

    void nonTextTypesAreRefusedBeforeTheBodyIsRead()
    {
        const QList<QByteArray> types = {"video/mp4", "image/png", "audio/mpeg", "application/pdf",
                                         "application/octet-stream", "application/zip"};
        for (const QByteArray &type : types)
        {
            const QString path = QStringLiteral("/t-") + QString::fromLatin1(type).replace(QLatin1Char('/'), QLatin1Char('-'));
            auto &r = srv->routes[path];
            r.contentType = type;
            r.streamBytes = qint64(500) * 1024 * 1024;
            // A cap far above the body, so only the response headers can stop this.
            WebFetchOptions opts = quick();
            opts.maxBytes = qint64(1) << 30;
            const Outcome o = run(*ws, srv->url(path), opts);
            QCOMPARE(o.calls, 1);
            QVERIFY2(o.error.contains(QLatin1String("not a text document")), qPrintable(type + ": " + o.error.toUtf8()));
            QVERIFY2(o.error.contains(QString::fromLatin1(type)), qPrintable(o.error));
            QVERIFY2(srv->sent[path] < 8 * 1024 * 1024, qPrintable(type + " sent " + QByteArray::number(srv->sent[path])));
        }
    }

    void binaryDataLabelledAsTextIsRefused()
    {
        auto &r = srv->routes[QStringLiteral("/bin")];
        r.contentType = "text/html";
        r.fill = QByteArray("\x00\x01\x02\x03\xff\xfe", 6);
        r.streamBytes = qint64(50) * 1024 * 1024;
        const Outcome o = run(*ws, srv->url(QStringLiteral("/bin")));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.contains(QLatin1String("binary")), qPrintable(o.error));
        QVERIFY2(srv->sent[QStringLiteral("/bin")] < 8 * 1024 * 1024, qPrintable(QString::number(srv->sent[QStringLiteral("/bin")])));
    }

    void smallBinaryBodyIsRefusedToo()
    {
        auto &r = srv->routes[QStringLiteral("/tiny")];
        r.contentType.clear();
        r.body = QByteArray("\x00\x01\x02\x03", 4);
        const Outcome o = run(*ws, srv->url(QStringLiteral("/tiny")));
        QVERIFY2(o.error.contains(QLatin1String("binary")), qPrintable(o.error));
    }

    void scriptAndStyleTypesGetTheHintInsteadOfTheBody()
    {
        auto &r = srv->routes[QStringLiteral("/lib")]; // no .js extension: decided from the header
        r.contentType = "application/javascript";
        r.streamBytes = qint64(50) * 1024 * 1024;
        const Outcome o = run(*ws, srv->url(QStringLiteral("/lib")));
        QVERIFY2(o.error.isEmpty(), qPrintable(o.error));
        QVERIFY(o.text.contains(QLatin1String("<script src=")));
        QVERIFY2(srv->sent[QStringLiteral("/lib")] < 8 * 1024 * 1024, qPrintable(QString::number(srv->sent[QStringLiteral("/lib")])));
    }

    void httpErrorsReportTheStatusWithoutDownloadingTheBody()
    {
        auto &r = srv->routes[QStringLiteral("/gone")];
        r.status = 404;
        r.reason = "Not Found";
        r.streamBytes = qint64(50) * 1024 * 1024;
        const Outcome o = run(*ws, srv->url(QStringLiteral("/gone")));
        QCOMPARE(o.calls, 1);
        QCOMPARE(o.error, QStringLiteral("HTTP 404 Not Found"));
        QVERIFY2(srv->sent[QStringLiteral("/gone")] < 8 * 1024 * 1024, qPrintable(QString::number(srv->sent[QStringLiteral("/gone")])));
    }

    // -- timing ----------------------------------------------------------------

    void serverThatGoesSilentIsAbandonedAfterTheStallTimeout()
    {
        srv->routes[QStringLiteral("/stall")].hold = true;
        const Outcome o = run(*ws, srv->url(QStringLiteral("/stall")), quick(300, 20000));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.contains(QLatin1String("no data received for 0.3 s")), qPrintable(o.error));
        QVERIFY2(o.ms >= 250 && o.ms < 3000, qPrintable(QString::number(o.ms)));
    }

    void steadyTrickleIsNotStalled()
    {
        // 2 seconds in total, but never 800 ms without more data: the stall timer
        // restarts each time, so this must complete.
        auto &r = srv->routes[QStringLiteral("/drip")];
        r.contentType = "text/plain";
        r.fill = "0123456789abcdef";
        r.chunk = 8;
        r.streamBytes = 160;
        r.delayMs = 100;
        const Outcome o = run(*ws, srv->url(QStringLiteral("/drip")), quick(800, 20000));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.isEmpty(), qPrintable(o.error));
        QVERIFY(o.text.contains(QLatin1String("01234567")));
        QVERIFY2(o.ms >= 1500, qPrintable(QString::number(o.ms))); // it really did take longer than the stall timeout
    }

    void totalTimeLimitBoundsATrickleThatNeverStalls()
    {
        auto &r = srv->routes[QStringLiteral("/endless")];
        r.contentType = "text/plain";
        r.endless = true;
        r.chunk = 8;
        r.delayMs = 100;
        const Outcome o = run(*ws, srv->url(QStringLiteral("/endless")), quick(800, 2000));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.contains(QLatin1String("gave up after 2 s")), qPrintable(o.error));
        QVERIFY2(o.ms >= 1800 && o.ms < 6000, qPrintable(QString::number(o.ms)));
    }

    void totalTimeNeverShorterThanTheStallTime()
    {
        srv->routes[QStringLiteral("/stall")].hold = true;
        WebFetchOptions opts;
        opts.stallMs = 600;
        opts.totalMs = 100; // nonsense: raised to the stall time, so this is not cut off at 100 ms
        const Outcome o = run(*ws, srv->url(QStringLiteral("/stall")), opts);
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.ms >= 500, qPrintable(QString::number(o.ms)));
        QVERIFY2(!o.error.isEmpty(), "expected a timeout");
    }

    // -- cancellation ----------------------------------------------------------

    void cancelAllAbortsAFetchInFlight()
    {
        srv->routes[QStringLiteral("/stall")].hold = true;
        Outcome out;
        ws->fetch(srv->url(QStringLiteral("/stall")),
                  [&out](QString text, QString error)
                  {
                      ++out.calls;
                      out.text = text;
                      out.error = error;
                  },
                  quick(60000, 120000));
        QVERIFY(waitFor([&]() { return srv->hits.value(QStringLiteral("/stall")) == 1; }));
        QCOMPARE(out.calls, 0);
        ws->cancelAll();
        QVERIFY(waitFor([&]() { return out.calls > 0; }, 2000));
        QTest::qWait(60);
        QCOMPARE(out.calls, 1);
        QCOMPARE(out.error, QStringLiteral("cancelled"));
    }

    void cancelAllWithNothingRunningIsHarmless()
    {
        ws->cancelAll();
        QVERIFY(true);
    }

    void destroyingTheFetcherMidFetchIsSafeAndSilent()
    {
        srv->routes[QStringLiteral("/stall")].hold = true;
        int calls = 0;
        ws->fetch(srv->url(QStringLiteral("/stall")), [&calls](QString, QString) { ++calls; }, quick(60000, 120000));
        QVERIFY(waitFor([&]() { return srv->hits.value(QStringLiteral("/stall")) == 1; }));
        ws.reset();
        QTest::qWait(200);
        QCOMPARE(calls, 0);
    }

    // -- redirects -------------------------------------------------------------

    void redirectsAreFollowed()
    {
        srv->routes[QStringLiteral("/a")].status = 302;
        srv->routes[QStringLiteral("/a")].reason = "Found";
        srv->routes[QStringLiteral("/a")].location = "/b";
        srv->routes[QStringLiteral("/a")].body = "moved";
        srv->routes[QStringLiteral("/b")].status = 301;
        srv->routes[QStringLiteral("/b")].location = srv->url(QStringLiteral("/c")).toLatin1(); // absolute
        srv->routes[QStringLiteral("/b")].body = "moved";
        srv->routes[QStringLiteral("/c")].body = "<p>the final page</p>";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/a")));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.isEmpty(), qPrintable(o.error));
        QVERIFY(o.text.contains(QLatin1String("the final page")));
        QVERIFY(o.text.startsWith(QStringLiteral("URL: ") + srv->url(QStringLiteral("/c"))));
    }

    void relativeRedirectFormsAreResolved()
    {
        srv->routes[QStringLiteral("/dir/start")].status = 302;
        srv->routes[QStringLiteral("/dir/start")].location = "next";          // relative to the directory
        srv->routes[QStringLiteral("/dir/start")].body = "moved";
        srv->routes[QStringLiteral("/dir/next")].status = 302;
        srv->routes[QStringLiteral("/dir/next")].location = "../up?x=1";      // parent directory + query
        srv->routes[QStringLiteral("/dir/next")].body = "moved";
        srv->routes[QStringLiteral("/up?x=1")].body = "<p>arrived</p>";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/dir/start")));
        QVERIFY2(o.error.isEmpty(), qPrintable(o.error));
        QVERIFY(o.text.contains(QLatin1String("arrived")));
    }

    void redirectToAPrivateAddressIsRefused()
    {
        auto &r = srv->routes[QStringLiteral("/to-lan")];
        r.status = 302;
        r.location = "http://10.1.2.3/secret";
        r.body = "moved";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/to-lan")));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.contains(QLatin1String("redirected to a URL that is not allowed")), qPrintable(o.error));
    }

    void redirectToAMappedPrivateAddressIsRefused()
    {
        auto &r = srv->routes[QStringLiteral("/to-mapped")];
        r.status = 307;
        r.location = "http://[::ffff:192.168.0.1]/admin";
        r.body = "moved";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/to-mapped")));
        QVERIFY2(o.error.contains(QLatin1String("not allowed")), qPrintable(o.error));
    }

    void redirectToAReservedNameIsRefused()
    {
        auto &r = srv->routes[QStringLiteral("/to-name")];
        r.status = 302;
        r.location = "http://router.lan/";
        r.body = "moved";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/to-name")));
        QVERIFY2(o.error.contains(QLatin1String("not allowed")), qPrintable(o.error));
    }

    void redirectToAHostnameThatResolvesToAPrivateAddressIsRefused()
    {
        QStringList looked;
        ws->setResolver([&looked](const QString &host, const WebSearch::ResolveDone &done)
                        {
                            looked << host;
                            done({QHostAddress(QStringLiteral("192.168.1.5"))}, {});
                        });
        auto &r = srv->routes[QStringLiteral("/to-evil")];
        r.status = 302;
        r.location = "http://evil.example.com/x";
        r.body = "moved";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/to-evil")));
        QCOMPARE(looked, QStringList{QStringLiteral("evil.example.com")});
        QVERIFY2(o.error.contains(QLatin1String("redirected to a URL that is not allowed")), qPrintable(o.error));
    }

    void redirectLoopStops()
    {
        auto &r = srv->routes[QStringLiteral("/loop")];
        r.status = 302;
        r.location = "/loop";
        r.body = "again";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/loop")));
        QCOMPARE(o.calls, 1);
        QCOMPARE(o.error, QStringLiteral("too many redirects"));
        QCOMPARE(srv->hits.value(QStringLiteral("/loop")), 6); // the request, then 5 followed hops
    }

    void redirectWithoutALocationFails()
    {
        auto &r = srv->routes[QStringLiteral("/nowhere")];
        r.status = 302;
        r.body = "moved";
        const Outcome o = run(*ws, srv->url(QStringLiteral("/nowhere")));
        QVERIFY2(o.error.contains(QLatin1String("without saying where")), qPrintable(o.error));
    }

    void hugeRedirectBodyIsNotAccepted()
    {
        auto &r = srv->routes[QStringLiteral("/fat")];
        r.status = 302;
        r.location = "/p";
        r.streamBytes = qint64(50) * 1024 * 1024;
        const Outcome o = run(*ws, srv->url(QStringLiteral("/fat")));
        QVERIFY2(o.error.contains(QLatin1String("too large")), qPrintable(o.error));
        QVERIFY2(srv->sent[QStringLiteral("/fat")] < 8 * 1024 * 1024, qPrintable(QString::number(srv->sent[QStringLiteral("/fat")])));
    }

    // -- which hosts may be contacted ------------------------------------------

    void literalPrivateAddressesAreRefusedWithoutAnyRequest()
    {
        int lookups = 0;
        ws->setResolver([&lookups](const QString &, const WebSearch::ResolveDone &done)
                        {
                            ++lookups;
                            done({}, QStringLiteral("unused"));
                        });
        for (const char *u : {"http://10.1.2.3/", "http://[fd00::1]/", "http://[::ffff:10.1.2.3]/", "http://router/",
                              "http://printer.local/"})
        {
            const Outcome o = run(*ws, QString::fromLatin1(u));
            QCOMPARE(o.calls, 1);
            QCOMPARE(o.error, QStringLiteral("that URL is not allowed"));
        }
        QCOMPARE(lookups, 0);
    }

    void aHostnameThatResolvesToAPrivateAddressIsRefusedBeforeConnecting()
    {
        QStringList looked;
        ws->setResolver([&looked](const QString &host, const WebSearch::ResolveDone &done)
                        {
                            looked << host;
                            done({QHostAddress(QStringLiteral("10.0.0.5"))}, {});
                        });
        // Even though something is listening on this port at 127.0.0.1, no request is made.
        const Outcome o = run(*ws, QStringLiteral("http://intranet.example.com:%1/").arg(srv->port()));
        QCOMPARE(o.calls, 1);
        QCOMPARE(o.error, QStringLiteral("that URL is not allowed"));
        QCOMPARE(looked, QStringList{QStringLiteral("intranet.example.com")});
        QVERIFY(srv->hits.isEmpty());
    }

    void oneBadAddressAmongPublicOnesIsEnough()
    {
        ws->setResolver([](const QString &, const WebSearch::ResolveDone &done)
                        {
                            done({QHostAddress(QStringLiteral("93.184.216.34")), QHostAddress(QStringLiteral("fd12::1"))}, {});
                        });
        const Outcome o = run(*ws, QStringLiteral("http://mixed.example.com:%1/").arg(srv->port()));
        QCOMPARE(o.error, QStringLiteral("that URL is not allowed"));
        QVERIFY(srv->hits.isEmpty());
    }

    void aFailedLookupIsReported()
    {
        ws->setResolver([](const QString &, const WebSearch::ResolveDone &done) { done({}, QStringLiteral("nxdomain")); });
        const Outcome o = run(*ws, QStringLiteral("http://nope.example.com/"));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.contains(QLatin1String("could not look up nope.example.com")), qPrintable(o.error));
    }

    void aHostnameWithOnlyPublicAddressesProceedsToConnect()
    {
        // The resolver says "public", so the request is attempted. There is no such
        // host in real DNS, so it fails on the network, not on the policy.
        ws->setResolver([](const QString &, const WebSearch::ResolveDone &done)
                        { done({QHostAddress(QStringLiteral("93.184.216.34"))}, {}); });
        const Outcome o = run(*ws, QStringLiteral("http://policy-test.invalid/"), quick(1500, 5000));
        QCOMPARE(o.calls, 1);
        QVERIFY2(!o.error.isEmpty() && !o.error.contains(QLatin1String("not allowed")), qPrintable(o.error));
    }

    void lookupTimeIsCoveredByTheTotalTimeLimit()
    {
        ws->setResolver([](const QString &, const WebSearch::ResolveDone &) { /* never answers */ });
        const Outcome o = run(*ws, QStringLiteral("http://slow-dns.example.com/"), quick(400, 900));
        QCOMPARE(o.calls, 1);
        QVERIFY2(o.error.contains(QLatin1String("gave up after")), qPrintable(o.error));
    }

    void addressAllowedTable()
    {
        const auto ok = [](const char *a) { return WebSearch::addressAllowed(QHostAddress(QString::fromLatin1(a))); };
        for (const char *bad : {"0.0.0.0", "0.1.2.3", "10.0.0.1", "100.64.0.1", "127.0.0.1", "169.254.169.254", "172.16.0.1",
                                "172.31.255.255", "192.0.0.1", "192.0.2.1", "192.168.1.1", "198.18.0.1", "198.19.255.255",
                                "198.51.100.1", "203.0.113.1", "224.0.0.1", "239.255.255.255", "240.0.0.1", "255.255.255.255",
                                "::", "::1", "::10.1.2.3", "::ffff:127.0.0.1", "::ffff:10.0.0.1", "::ffff:192.168.1.1",
                                "::ffff:169.254.169.254", "64:ff9b::a01:203", "2002:0a01:0203::1", "2002:c0a8:0101::",
                                "2001:0:4136:e378:8000:63bf:3fff:fdd2", "2001:db8::1", "fc00::1", "fd12:3456::1", "fe80::1",
                                "fec0::1", "ff02::1", "100::1"})
            QVERIFY2(!ok(bad), bad);
        for (const char *good : {"1.1.1.1", "8.8.8.8", "93.184.216.34", "100.63.255.255", "100.128.0.1", "172.15.255.255",
                                 "172.32.0.1", "169.253.0.1", "192.0.1.1", "192.169.0.1", "198.17.255.255", "198.20.0.1",
                                 "203.0.114.1", "223.255.255.255", "2606:4700::1111", "2001:4860:4860::8888",
                                 "2620:0:ccc::2", "::ffff:8.8.8.8", "64:ff9b::808:808", "2002:0808:0808::1"})
            QVERIFY2(ok(good), good);
        QVERIFY(!WebSearch::addressAllowed(QHostAddress()));
    }
};

QTEST_GUILESS_MAIN(TestWebFetch)
#include "test_web_fetch.moc"
