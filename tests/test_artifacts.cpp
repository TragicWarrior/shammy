#include "artifacts/ArtifactExtractor.h"
#include "artifacts/ArtifactMarkup.h"
#include "artifacts/ContentSplitter.h"
#include "artifacts/HtmlDocument.h"

#include <QTextBlock>
#include <QTextDocument>
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

    void unfencedHtmlDocument()
    {
        QString html = QStringLiteral("<!DOCTYPE html>\n<html><body>");
        for (int i = 0; i < 40; ++i)
        {
            html += QStringLiteral("<p>row %1</p>").arg(i);
        }
        html += QStringLiteral("</body></html>");
        const QString s = QStringLiteral("Here is the report:\n\n") + html;
        const auto d = ArtifactExtractor::extract(s);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].type, QString("text/html"));
        QVERIFY(d[0].content.startsWith(QLatin1String("<!DOCTYPE html>")));
        QVERIFY(!d[0].content.startsWith(QLatin1String("Here is")));
    }

    void shortHtmlMentionIgnored()
    {
        const auto d = ArtifactExtractor::extract(
            QStringLiteral("Use an <html> tag if you need one."));
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

    // -- a single "~" must not start a strike-through ----------------------------

    static QString struck(const QString &markdown)
    {
        QTextDocument d;
        d.setMarkdown(markdown);
        QString out;
        for (QTextBlock b = d.begin(); b.isValid(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().fontStrikeOut())
                    out += it.fragment().text();
        return out;
    }
    static QString shown(const QString &markdown)
    {
        QTextDocument d;
        d.setMarkdown(markdown);
        return d.toPlainText();
    }

    void aboutFiguresAreNotStruckThrough()
    {
        const QString reply = QStringLiteral("pack voltage (~53 V), so it shifts: roughly 16 mA near the bottom (~50 V) and about 14 mA near full (~57 V).");
        // What went wrong: the renderer pairs the first two tildes. Not checked
        // here, because it depends on the md4c Qt was built with (Homebrew's
        // Qt 6.11 leaves them alone).
        const QString fixed = ContentSplitter::escapeLoneTildes(reply);
        QCOMPARE(struck(fixed), QString());
        QCOMPARE(shown(fixed), reply); // reads exactly as written, tildes and all
    }

    void realStrikeThroughStillWorks()
    {
        const QString fixed = ContentSplitter::escapeLoneTildes(QStringLiteral("keep ~~this~~ but not ~5 V or ~6 V"));
        QCOMPARE(struck(fixed), QStringLiteral("this"));
        QCOMPARE(shown(fixed), QStringLiteral("keep this but not ~5 V or ~6 V"));
    }

    void tildesInCodeAndAddressesAreLeftAlone()
    {
        QCOMPARE(ContentSplitter::escapeLoneTildes(QStringLiteral("run `cd ~/src` then `ls ~`")), QStringLiteral("run `cd ~/src` then `ls ~`"));
        QCOMPARE(ContentSplitter::escapeLoneTildes(QStringLiteral("``a ~ b`` and ~c")), QStringLiteral("``a ~ b`` and \\~c"));
        QCOMPARE(ContentSplitter::escapeLoneTildes(QStringLiteral("see https://example.org/~user/page and www.x.org/~u")),
                 QStringLiteral("see https://example.org/~user/page and www.x.org/~u"));
        // The code span shows its tilde with no backslash.
        QCOMPARE(shown(ContentSplitter::escapeLoneTildes(QStringLiteral("home is `~` and about ~3 m, ~4 m"))),
                 QStringLiteral("home is ~ and about ~3 m, ~4 m"));
    }

    void otherTildeFormsAreUntouched()
    {
        QCOMPARE(ContentSplitter::escapeLoneTildes(QStringLiteral("no tildes here")), QStringLiteral("no tildes here"));
        QCOMPARE(ContentSplitter::escapeLoneTildes(QString()), QString());
        QCOMPARE(ContentSplitter::escapeLoneTildes(QStringLiteral("already \\~ escaped")), QStringLiteral("already \\~ escaped"));
        QCOMPARE(ContentSplitter::escapeLoneTildes(QStringLiteral("~~~\nfenced\n~~~")), QStringLiteral("~~~\nfenced\n~~~"));
        QCOMPARE(ContentSplitter::escapeLoneTildes(QStringLiteral("~")), QStringLiteral("\\~"));
        QCOMPARE(ContentSplitter::escapeLoneTildes(QStringLiteral("a~b~c")), QStringLiteral("a\\~b\\~c"));
        QCOMPARE(ContentSplitter::escapeLoneTildes(QStringLiteral("open `code and ~x")), QStringLiteral("open `code and \\~x"));
    }
};

QTEST_MAIN(TestArtifacts)
#include "test_artifacts.moc"
