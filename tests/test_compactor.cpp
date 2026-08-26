#include "compact/Compactor.h"

#include <QStringList>
#include <QtTest>

class TestCompactor : public QObject
{
    Q_OBJECT
private:
    static ChatMessage msg(const QString &role, const QString &content)
    {
        ChatMessage m;
        m.role = role;
        m.content = content;
        return m;
    }

private slots:
    void clampThreshold()
    {
        QCOMPARE(Compact::clampThreshold(80), 80);
        QCOMPARE(Compact::clampThreshold(10), Compact::kMinThresholdPct);
        QCOMPARE(Compact::clampThreshold(99), Compact::kMaxThresholdPct);
        QCOMPARE(Compact::clampThreshold(90), 90);
    }

    void parsePlainText()
    {
        const auto c = Compact::parseCommand(QStringLiteral("hello"));
        QVERIFY(c.name.isEmpty());
        QVERIFY(!c.passthrough);
    }

    void parseCompact()
    {
        const auto c = Compact::parseCommand(QStringLiteral("/compact"));
        QCOMPARE(c.name, QString("compact"));
        QVERIFY(c.args.isEmpty());
        QVERIFY(!c.passthrough);
    }

    void parseCompactArgs()
    {
        const auto c = Compact::parseCommand(QStringLiteral("/compact keep the auth bug"));
        QCOMPARE(c.name, QString("compact"));
        QCOMPARE(c.args, QString("keep the auth bug"));
    }

    void parseHelpCase()
    {
        const auto c = Compact::parseCommand(QStringLiteral("/Help"));
        QCOMPARE(c.name, QString("help"));
    }

    void parseEscape()
    {
        const auto c = Compact::parseCommand(QStringLiteral("//compact"));
        QVERIFY(c.name.isEmpty());
        QVERIFY(c.passthrough);
        QCOMPARE(c.args, QString("/compact"));
    }

    void parseBareSlash()
    {
        const auto c = Compact::parseCommand(QStringLiteral("/"));
        QVERIFY(c.name.isEmpty());
        QVERIFY(!c.passthrough);
    }

    void slashCatalog()
    {
        const auto all = Compact::slashCommands();
        QStringList names;
        for (const auto &c : all)
            names.append(c.name);
        QVERIFY(names.contains(QStringLiteral("compact")));
        QVERIFY(names.contains(QStringLiteral("help")));
        QVERIFY(names.contains(QStringLiteral("new")));
        QVERIFY(names.contains(QStringLiteral("quit")));
        QVERIFY(Compact::helpText().contains(QStringLiteral("/quit")));
        QVERIFY(Compact::unknownCommandText(QStringLiteral("nope")).contains(QStringLiteral("/nope")));
    }

    void matchingSlash()
    {
        QCOMPARE(Compact::matchingSlash(QStringLiteral("/")).size(), Compact::slashCommands().size());
        const auto c = Compact::matchingSlash(QStringLiteral("/c"));
        QCOMPARE(c.size(), 1);
        QCOMPARE(c.at(0).name, QString("compact"));
        const auto q = Compact::matchingSlash(QStringLiteral("/qu"));
        QCOMPARE(q.size(), 1);
        QCOMPARE(q.at(0).name, QString("quit"));
        QVERIFY(Compact::matchingSlash(QStringLiteral("/compact ")).isEmpty());
        QVERIFY(Compact::matchingSlash(QStringLiteral("//quit")).isEmpty());
        QVERIFY(Compact::matchingSlash(QStringLiteral("hello")).isEmpty());
        QVERIFY(Compact::matchingSlash(QStringLiteral("/nope")).isEmpty());
    }

    void planTooShort()
    {
        QVector<ChatMessage> msgs;
        msgs.append(msg(QStringLiteral("user"), QStringLiteral("hi")));
        msgs.append(msg(QStringLiteral("assistant"), QStringLiteral("hello")));
        QVERIFY(!Compact::plan(msgs).canCompact());
        QVERIFY(!Compact::plan(msgs, 1).canCompact());
    }

    void planManualKeepsLastUserTurn()
    {
        QVector<ChatMessage> msgs;
        msgs.append(msg(QStringLiteral("user"), QStringLiteral("one")));
        msgs.append(msg(QStringLiteral("assistant"), QStringLiteral("a1")));
        msgs.append(msg(QStringLiteral("user"), QStringLiteral("two")));
        msgs.append(msg(QStringLiteral("assistant"), QStringLiteral("a2")));
        QVERIFY(!Compact::plan(msgs, 2).canCompact());
        const auto p = Compact::plan(msgs, 1);
        QVERIFY(p.canCompact());
        QCOMPARE(p.summarized.size(), 2);
        QCOMPARE(p.tail.at(0).content, QString("two"));
    }

    void planKeepsLastTwoUserTurns()
    {
        QVector<ChatMessage> msgs;
        msgs.append(msg(QStringLiteral("user"), QStringLiteral("one")));
        msgs.append(msg(QStringLiteral("assistant"), QStringLiteral("a1")));
        msgs.append(msg(QStringLiteral("user"), QStringLiteral("two")));
        msgs.append(msg(QStringLiteral("assistant"), QStringLiteral("a2")));
        msgs.append(msg(QStringLiteral("user"), QStringLiteral("three")));
        msgs.append(msg(QStringLiteral("assistant"), QStringLiteral("a3")));
        const auto p = Compact::plan(msgs, 2);
        QVERIFY(p.canCompact());
        QCOMPARE(p.summarized.size(), 2);
        QCOMPARE(p.summarized.at(0).content, QString("one"));
        QCOMPARE(p.tail.size(), 4);
        QCOMPARE(p.tail.at(0).content, QString("two"));
    }

    void planDoesNotSplitToolTurn()
    {
        QVector<ChatMessage> msgs;
        msgs.append(msg(QStringLiteral("user"), QStringLiteral("old")));
        msgs.append(msg(QStringLiteral("assistant"), QStringLiteral("old-a")));
        msgs.append(msg(QStringLiteral("user"), QStringLiteral("use tool")));
        ChatMessage asst;
        asst.role = QStringLiteral("assistant");
        asst.content = QStringLiteral("calling");
        asst.toolCallsJson = QStringLiteral("[{}]");
        msgs.append(asst);
        ChatMessage tool;
        tool.role = QStringLiteral("tool");
        tool.content = QStringLiteral("result");
        msgs.append(tool);
        msgs.append(msg(QStringLiteral("assistant"), QStringLiteral("done")));
        const auto p = Compact::plan(msgs, 1);
        QVERIFY(p.canCompact());
        QCOMPARE(p.tail.size(), 4);
        QCOMPARE(p.tail.at(0).role, QString("user"));
        QCOMPARE(p.tail.at(1).role, QString("assistant"));
        QCOMPARE(p.tail.at(2).role, QString("tool"));
    }

    void transcriptAndRequest()
    {
        QVector<ChatMessage> msgs;
        msgs.append(msg(QStringLiteral("user"), QStringLiteral("hi")));
        const QString t = Compact::transcript(msgs);
        QVERIFY(t.contains(QStringLiteral("user:")));
        QVERIFY(t.contains(QStringLiteral("hi")));
        const auto req = Compact::summaryRequest(t, QStringLiteral("keep paths"));
        QCOMPARE(req.size(), 2);
        QCOMPARE(req.at(0).role, QString("system"));
        QVERIFY(req.at(1).content.contains(QStringLiteral("keep paths")));
    }
};

QTEST_MAIN(TestCompactor)
#include "test_compactor.moc"
