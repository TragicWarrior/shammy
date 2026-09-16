#include "websearch/WebSearch.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QtTest>

class TestWebSearch : public QObject
{
    Q_OBJECT
private slots:
    void brave()
    {
        const QJsonObject body{
            {QStringLiteral("web"),
             QJsonObject{{QStringLiteral("results"),
                          QJsonArray{QJsonObject{{QStringLiteral("title"), QStringLiteral("Ada")},
                                                 {QStringLiteral("url"), QStringLiteral("https://a.example")},
                                                 {QStringLiteral("description"), QStringLiteral("Countess of Lovelace")}}}}}},
        };
        const QString s = WebSearch::formatBrave(body);
        QVERIFY(s.contains(QStringLiteral("Ada")));
        QVERIFY(s.contains(QStringLiteral("https://a.example")));
        QVERIFY(s.contains(QStringLiteral("Countess")));
    }

    void tavily()
    {
        const QJsonObject body{
            {QStringLiteral("answer"), QStringLiteral("Yes.")},
            {QStringLiteral("results"),
             QJsonArray{QJsonObject{{QStringLiteral("title"), QStringLiteral("T")},
                                    {QStringLiteral("url"), QStringLiteral("https://t.example")},
                                    {QStringLiteral("content"), QStringLiteral("body")}}}},
        };
        const QString s = WebSearch::formatTavily(body);
        QVERIFY(s.contains(QStringLiteral("Summary: Yes.")));
        QVERIFY(s.contains(QStringLiteral("https://t.example")));
    }

    void exa()
    {
        const QJsonObject body{
            {QStringLiteral("results"),
             QJsonArray{QJsonObject{{QStringLiteral("title"), QStringLiteral("E")},
                                    {QStringLiteral("url"), QStringLiteral("https://e.example")},
                                    {QStringLiteral("text"), QStringLiteral("neural hit")}}}},
        };
        const QString s = WebSearch::formatExa(body);
        QVERIFY(s.contains(QStringLiteral("neural hit")));
        QVERIFY(s.contains(QStringLiteral("https://e.example")));
    }

    void toolName()
    {
        QCOMPARE(WebSearch::toolDefinition().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("web_search"));
        QCOMPARE(WebSearch::fetchToolDefinition().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("web_fetch"));
    }

    void urlAllowed()
    {
        QVERIFY(WebSearch::urlAllowed(QUrl(QStringLiteral("https://github.com/foo/bar"))));
        QVERIFY(WebSearch::urlAllowed(QUrl(QStringLiteral("http://example.com/a"))));
        QVERIFY(!WebSearch::urlAllowed(QUrl(QStringLiteral("file:///etc/passwd"))));
        QVERIFY(!WebSearch::urlAllowed(QUrl(QStringLiteral("https://localhost/x"))));
        QVERIFY(!WebSearch::urlAllowed(QUrl(QStringLiteral("http://127.0.0.1/x"))));
        QVERIFY(!WebSearch::urlAllowed(QUrl(QStringLiteral("http://192.168.1.1/x"))));
        QVERIFY(!WebSearch::urlAllowed(QUrl(QStringLiteral("http://10.0.0.5/x"))));
        QVERIFY(!WebSearch::urlAllowed(QUrl(QStringLiteral("https://169.254.169.254/latest"))));
    }

    void canonicalizeGitHub()
    {
        QCOMPARE(WebSearch::canonicalizeFetchUrl(QUrl(QStringLiteral("https://github.com/foo/bar"))).toString(),
                 QString("https://raw.githubusercontent.com/foo/bar/HEAD/README.md"));
        QCOMPARE(WebSearch::canonicalizeFetchUrl(
                     QUrl(QStringLiteral("https://github.com/foo/bar/blob/main/docs/README.md"))).toString(),
                 QString("https://raw.githubusercontent.com/foo/bar/main/docs/README.md"));
        QCOMPARE(WebSearch::canonicalizeFetchUrl(QUrl(QStringLiteral("https://example.com/x"))).toString(),
                 QString("https://example.com/x"));
    }

    void extractHtmlAndPlain()
    {
        const QString html = WebSearch::extractText(
            QByteArray("<html><head><title>Hi</title><script>secret()</script></head>"
                       "<body><h1>Hello</h1><p>World &amp; friends</p></body></html>"),
            QStringLiteral("text/html; charset=utf-8"));
        QVERIFY(html.contains(QStringLiteral("Hello")));
        QVERIFY(html.contains(QStringLiteral("World & friends")));
        QVERIFY(!html.contains(QStringLiteral("secret")));
        QCOMPARE(WebSearch::extractText(QByteArray("# Title\n\nbody"), QStringLiteral("text/markdown")),
                 QString("# Title\n\nbody"));
        QVERIFY(WebSearch::extractText(QByteArray("PNG"), QStringLiteral("image/png")).isEmpty());
        QVERIFY(WebSearch::extractText(QByteArray("function(){}"), QStringLiteral("application/javascript"))
                    .isEmpty());
    }

    void staticAssetsNotInlined()
    {
        const QUrl js(QStringLiteral("https://cdn.jsdelivr.net/npm/chart.js@4.4.7/dist/chart.umd.min.js"));
        QVERIFY(WebSearch::isStaticAssetUrl(js));
        QVERIFY(WebSearch::isStaticAssetUrl(QUrl(QStringLiteral("https://unpkg.com/foo/bar.css"))));
        QVERIFY(!WebSearch::isStaticAssetUrl(QUrl(QStringLiteral("https://example.com/docs/readme.md"))));
        const QString hint = WebSearch::staticAssetHint(js);
        QVERIFY(hint.contains(QStringLiteral("<script src=")));
        QVERIFY(!hint.contains(QStringLiteral("function")));

        QString blob = QStringLiteral("URL: %1\n\n").arg(js.toString());
        blob += QString(80000, QLatin1Char('x'));
        const QString clipped = WebSearch::clipForModel(QStringLiteral("[web_fetch]\n") + blob);
        QVERIFY(clipped.size() < 2000);
        QVERIFY(clipped.contains(QStringLiteral("<script src=")));
        QVERIFY(!clipped.contains(QString(100, QLatin1Char('x'))));
    }
};

QTEST_MAIN(TestWebSearch)
#include "test_websearch.moc"
