#include "Util.h"

#include <QtTest>

class TestReply : public QObject
{
    Q_OBJECT
private slots:
    void passthroughWhenNoQuote()
    {
        QCOMPARE(formatReplyPrompt({}, QStringLiteral("hello")), QStringLiteral("hello"));
        QCOMPARE(formatReplyPrompt(QStringLiteral("   "), QStringLiteral("  hi  ")),
                 QStringLiteral("hi"));
    }

    void quoteOnly()
    {
        QCOMPARE(formatReplyPrompt(QStringLiteral("excerpt"), {}), QStringLiteral("> excerpt\n"));
    }

    void quoteAndBody()
    {
        const QString got = formatReplyPrompt(QStringLiteral("one\ntwo"), QStringLiteral("why?"));
        QCOMPARE(got, QStringLiteral("> one\n> two\n\nwhy?"));
    }

    void trimsCrlf()
    {
        const QString got = formatReplyPrompt(QStringLiteral("a\r\nb\r"), QStringLiteral("c"));
        QCOMPARE(got, QStringLiteral("> a\n> b\n\nc"));
    }
};

QTEST_MAIN(TestReply)
#include "test_reply.moc"
