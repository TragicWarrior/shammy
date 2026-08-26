#include "artifacts/ArtifactExtractor.h"
#include "artifacts/ArtifactMarkup.h"
#include "artifacts/ContentSplitter.h"
#include "artifacts/HtmlDocument.h"

#include <QtTest>

class TestArtifacts : public QObject
{
    Q_OBJECT
private slots:
    void taggedBlock()
    {
        const QString s = QStringLiteral(
            "Here you go\n"
            "<artifact identifier=\"dash\" type=\"text/html\" title=\"Dash\">\n"
            "<h1>Hi</h1>\n"
            "</artifact>\n");
        const auto d = ArtifactExtractor::extract(s);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].identifier, QString("dash"));
        QCOMPARE(d[0].title, QString("Dash"));
        QCOMPARE(d[0].type, QString("text/html"));
        QVERIFY(d[0].content.contains("<h1>Hi</h1>"));
    }

    void splitterFindsArtifactAndCode()
    {
        const QString s = QStringLiteral(
            "intro\n```js\nconsole.log(1)\n```\n"
            "<artifact identifier=\"a\" type=\"text/plain\" title=\"t\">body</artifact>");
        const auto parts = ContentSplitter::split(s);
        QVERIFY(parts.size() >= 2);
        bool sawCode = false, sawArt = false;
        for (const auto &p : parts)
        {
            if (p.type == QLatin1String("code"))
                sawCode = true;
            if (p.type == QLatin1String("artifact"))
            {
                sawArt = true;
                QCOMPARE(p.identifier, QString("a"));
            }
        }
        QVERIFY(sawCode);
        QVERIFY(sawArt);
    }

    void fencePromote()
    {
        QString body;
        for (int i = 0; i < 16; ++i)
            body += QStringLiteral("<p>%1</p>\n").arg(i);
        const QString s = QStringLiteral("```html\n") + body + QStringLiteral("```");
        const auto d = ArtifactExtractor::extract(s);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].type, QString("text/html"));
    }

    void markdownFencePromote()
    {
        QString body;
        for (int i = 0; i < 16; ++i)
            body += QStringLiteral("## heading %1\n\nparagraph %1\n").arg(i);
        const QString s = QStringLiteral("```markdown\n") + body + QStringLiteral("```");
        const auto d = ArtifactExtractor::extract(s);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].type, QString("text/markdown"));
    }

    void tinyFenceIgnored()
    {
        const QString s = QStringLiteral("```html\n<p>x</p>\n```");
        const auto d = ArtifactExtractor::extract(s);
        QVERIFY(d.isEmpty());
    }

    void splitsMarkdownTable()
    {
        const QString s = QStringLiteral(
            "before\n"
            "| a | b |\n"
            "| --- | --- |\n"
            "| 1 | 2 |\n"
            "after");
        const auto parts = ContentSplitter::split(s);
        QVERIFY(parts.size() >= 3);
        QCOMPARE(parts[0].type, QString("text"));
        QVERIFY(parts[0].text.contains("before"));
        QCOMPARE(parts[1].type, QString("table"));
        QVERIFY(parts[1].text.contains("\"headers\""));
        QVERIFY(parts[1].text.contains("\"a\""));
        QCOMPARE(parts[2].type, QString("text"));
        QVERIFY(parts[2].text.contains("after"));
    }

    void guessMime()
    {
        QCOMPARE(ArtifactExtractor::guessMime("html"), QString("text/html"));
        QCOMPARE(ArtifactExtractor::guessMime("image/svg+xml"), QString("image/svg+xml"));
        QCOMPARE(ArtifactExtractor::guessMime("python"), QString("text/x-python"));
    }

    void htmlFragmentBecomesDocument()
    {
        const QString out = HtmlDocument::complete(QStringLiteral("<h1>Hi</h1>"),
                                                   QStringLiteral("text/html"),
                                                   QStringLiteral("Dash"));
        QVERIFY(out.contains(QStringLiteral("<!DOCTYPE html>")));
        QVERIFY(out.contains(QStringLiteral("<h1>Hi</h1>")));
        QVERIFY(out.contains(QStringLiteral("<title>Dash</title>")));
        QVERIFY(out.contains(QStringLiteral("charset")));
    }

    void htmlCompleteDocumentUntouched()
    {
        const QString src = QStringLiteral(
            "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>X</title></head>"
            "<body><script>console.log(1)</script></body></html>");
        QCOMPARE(HtmlDocument::complete(src, QStringLiteral("text/html"), QStringLiteral("X")), src);
    }

    void htmlInjectsCharset()
    {
        const QString src = QStringLiteral(
            "<html><head><title>X</title></head><body>hi</body></html>");
        const QString out = HtmlDocument::complete(src, QStringLiteral("text/html"), {});
        QVERIFY(out.contains(QStringLiteral("charset")));
        QVERIFY(out.contains(QStringLiteral("<title>X</title>")));
    }

    void htmlWrapsSvg()
    {
        const QString out = HtmlDocument::complete(QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>"),
                                                   QStringLiteral("image/svg+xml"),
                                                   QStringLiteral("Icon"));
        QVERIFY(out.contains(QStringLiteral("<!DOCTYPE html>")));
        QVERIFY(out.contains(QStringLiteral("<svg")));
    }

    void markupHelpers()
    {
        QVERIFY(ArtifactMarkup::isPromoteLanguage(QStringLiteral("html")));
        QVERIFY(ArtifactMarkup::isPromoteLanguage(QStringLiteral("markdown")));
        QVERIFY(!ArtifactMarkup::isPromoteLanguage(QStringLiteral("python")));
        QVERIFY(ArtifactMarkup::isMdSeparatorCell(QStringLiteral(":---")));
        QVERIFY(!ArtifactMarkup::isMdSeparatorCell(QStringLiteral("abc")));
        QCOMPARE(ArtifactMarkup::attr(QStringLiteral("identifier=\"dash\" title=\"T\""), "identifier"),
                 QString("dash"));
        QCOMPARE(ArtifactMarkup::attr(QStringLiteral("identifier=\"dash\" title=\"T\""), "title"),
                 QString("T"));
    }

    void htmlTypeChecks()
    {
        QVERIFY(HtmlDocument::isHtmlType(QStringLiteral("text/html")));
        QVERIFY(HtmlDocument::isSvgType(QStringLiteral("image/svg+xml")));
        QVERIFY(HtmlDocument::isMarkdownType(QStringLiteral("text/markdown")));
        QVERIFY(HtmlDocument::isMarkdownType(QStringLiteral("markdown")));
        QVERIFY(HtmlDocument::isCompleteDocument(QStringLiteral("<!DOCTYPE html><html></html>")));
        QVERIFY(!HtmlDocument::isCompleteDocument(QStringLiteral("<h1>nope</h1>")));
    }
};

QTEST_MAIN(TestArtifacts)
#include "test_artifacts.moc"
