#include "Util.h"
#include "import/ClaudeChatMerge.h"
#include "persist/Store.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

// Importing a Claude conversation more than once: what came from Claude is
// refreshed, and what was added here is kept.

namespace
{
QJsonObject msg(const QString &uuid, const QString &sender, const QString &text, const QString &at = {})
{
    QJsonObject o{{QStringLiteral("uuid"), uuid},
                  {QStringLiteral("sender"), sender},
                  {QStringLiteral("content"),
                   QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}}}}};
    if (!at.isEmpty())
        o.insert(QStringLiteral("created_at"), at);
    return o;
}

QJsonObject chat(const QString &uuid, const QString &name, const QJsonArray &messages, bool starred = false)
{
    return {{QStringLiteral("uuid"), uuid},
            {QStringLiteral("name"), name},
            {QStringLiteral("is_starred"), starred},
            {QStringLiteral("created_at"), QStringLiteral("2026-05-01T10:00:00.000Z")},
            {QStringLiteral("updated_at"), QStringLiteral("2026-05-01T11:00:00.000Z")},
            {QStringLiteral("chat_messages"), messages}};
}

const QString kT1 = QStringLiteral("2026-05-01T10:00:01.000Z");
const QString kT2 = QStringLiteral("2026-05-01T10:00:02.000Z");
const QString kT3 = QStringLiteral("2026-05-01T10:00:03.000Z");
const QString kT4 = QStringLiteral("2026-05-01T10:00:04.000Z");
} // namespace

class TestClaudeImport : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<Store> store;
    QString project;

    QStringList contents(const QString &conv) const
    {
        QStringList out;
        for (const Message &m : store->messages(conv))
            out << m.content;
        return out;
    }
    Message local(const QString &conv, const QString &role, const QString &text, qint64 at)
    {
        Message m;
        m.id = newId();
        m.conversationId = conv;
        m.role = role;
        m.content = text;
        m.createdAt = at;
        store->upsertMessage(m);
        return m;
    }
    int rawCount(const QString &sql) const
    {
        const QString conn = QStringLiteral("count-") + newId();
        int n = -1;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(store->dbPath());
            if (db.open())
            {
                QSqlQuery q(db);
                if (q.exec(sql) && q.next())
                    n = q.value(0).toInt();
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(conn);
        return n;
    }

private slots:
    void init()
    {
        dir = std::make_unique<QTemporaryDir>();
        QVERIFY(dir->isValid());
        store = std::make_unique<Store>();
        QVERIFY(store->open(dir->filePath(QStringLiteral("t.db"))));
        Project p;
        p.id = newId();
        p.name = QStringLiteral("Imported project");
        p.createdAt = p.updatedAt = nowMs();
        store->upsertProject(p);
        project = p.id;
    }
    void cleanup()
    {
        store.reset();
        dir.reset();
    }

    void aFirstImportCreatesTheChat()
    {
        const auto out = ClaudeChatMerge::merge(
            store.get(), project, QStringLiteral("model-a"),
            chat(QStringLiteral("c1"), QStringLiteral("Trip plan"),
                 {msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("where to?"), kT1),
                  msg(QStringLiteral("m2"), QStringLiteral("assistant"), QStringLiteral("Lisbon"), kT2)},
                 true));
        QVERIFY(out.created);
        QCOMPARE(out.conversationId, QStringLiteral("c1"));
        QCOMPARE(out.messages, 2);
        const Conversation c = store->conversation(QStringLiteral("c1"));
        QCOMPARE(c.title, QStringLiteral("Trip plan"));
        QCOMPARE(c.projectId, project);
        QCOMPARE(c.model, QStringLiteral("model-a"));
        QVERIFY(c.pinned);
        const auto msgs = store->messages(QStringLiteral("c1"));
        QCOMPARE(msgs.size(), 2);
        QCOMPARE(msgs.at(0).role, QStringLiteral("user"));
        QCOMPARE(msgs.at(1).role, QStringLiteral("assistant"));
        QCOMPARE(contents(QStringLiteral("c1")), (QStringList{"where to?", "Lisbon"}));
    }

    void importingAgainKeepsWhatWasAddedHere()
    {
        const QJsonArray first{msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("where to?"), kT1),
                               msg(QStringLiteral("m2"), QStringLiteral("assistant"), QStringLiteral("Lisbon"), kT2)};
        ClaudeChatMerge::merge(store.get(), project, QStringLiteral("model-a"),
                               chat(QStringLiteral("c1"), QStringLiteral("Trip plan"), first, true));

        // Carry the chat on locally, and make it the user's own.
        const qint64 later = QDateTime::fromString(QStringLiteral("2026-06-01T00:00:00Z"), Qt::ISODate).toMSecsSinceEpoch();
        local(QStringLiteral("c1"), QStringLiteral("user"), QStringLiteral("and after that?"), later);
        local(QStringLiteral("c1"), QStringLiteral("assistant"), QStringLiteral("Porto"), later + 1);
        Conversation c = store->conversation(QStringLiteral("c1"));
        c.title = QStringLiteral("Portugal");
        c.model = QStringLiteral("qwen-local");
        c.backendId = QStringLiteral("backend-1");
        c.reasoningEffort = QStringLiteral("high");
        c.pinned = false;
        c.updatedAt = later + 1;
        store->upsertConversation(c);

        // Claude's copy has moved on too: one reply reworded, one message more.
        const QJsonArray second{msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("where to?"), kT1),
                                msg(QStringLiteral("m2"), QStringLiteral("assistant"), QStringLiteral("Lisbon, in May"), kT2),
                                msg(QStringLiteral("m3"), QStringLiteral("human"), QStringLiteral("how long?"), kT3)};
        const auto out = ClaudeChatMerge::merge(store.get(), project, QStringLiteral("model-b"),
                                                chat(QStringLiteral("c1"), QStringLiteral("Trip plan (renamed in Claude)"), second, true));
        QVERIFY(!out.created);
        QCOMPARE(out.conversationId, QStringLiteral("c1"));
        QCOMPARE(contents(QStringLiteral("c1")),
                 (QStringList{"where to?", "Lisbon, in May", "how long?", "and after that?", "Porto"}));
        c = store->conversation(QStringLiteral("c1"));
        QCOMPARE(c.title, QStringLiteral("Portugal"));
        QCOMPARE(c.model, QStringLiteral("qwen-local"));
        QCOMPARE(c.backendId, QStringLiteral("backend-1"));
        QCOMPARE(c.reasoningEffort, QStringLiteral("high"));
        QVERIFY(!c.pinned);
        QCOMPARE(c.updatedAt, later + 1); // not moved back to Claude's older date
        QCOMPARE(store->conversations(project).size(), 1);
    }

    void messagesGoneFromClaudeAreRemovedButLocalOnesStay()
    {
        ClaudeChatMerge::merge(store.get(), project, {},
                               chat(QStringLiteral("c1"), QStringLiteral("T"),
                                    {msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("q1"), kT1),
                                     msg(QStringLiteral("m2"), QStringLiteral("assistant"), QStringLiteral("a1"), kT2),
                                     msg(QStringLiteral("m3"), QStringLiteral("human"), QStringLiteral("q2"), kT3)}));
        local(QStringLiteral("c1"), QStringLiteral("user"), QStringLiteral("mine"), nowMs());
        // In Claude the second question was edited, which gives it a new id.
        ClaudeChatMerge::merge(store.get(), project, {},
                               chat(QStringLiteral("c1"), QStringLiteral("T"),
                                    {msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("q1"), kT1),
                                     msg(QStringLiteral("m2"), QStringLiteral("assistant"), QStringLiteral("a1"), kT2),
                                     msg(QStringLiteral("m4"), QStringLiteral("human"), QStringLiteral("q2 edited"), kT4)}));
        QCOMPARE(contents(QStringLiteral("c1")), (QStringList{"q1", "a1", "q2 edited", "mine"}));
    }

    void aLocalChatWithTheSameTitleIsLeftAlone()
    {
        // B8: this chat used to be taken over and emptied.
        Conversation mine;
        mine.id = newId();
        mine.projectId = project;
        mine.title = QStringLiteral("Trip plan");
        mine.createdAt = mine.updatedAt = nowMs();
        store->upsertConversation(mine);
        local(mine.id, QStringLiteral("user"), QStringLiteral("my own notes"), nowMs());

        const auto out = ClaudeChatMerge::merge(
            store.get(), project, {},
            chat(QStringLiteral("c1"), QStringLiteral("Trip plan"),
                 {msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("where to?"), kT1)}));
        QVERIFY(out.created);
        QCOMPARE(out.conversationId, QStringLiteral("c1"));
        QCOMPARE(contents(mine.id), QStringList{QStringLiteral("my own notes")});
        QCOMPARE(contents(QStringLiteral("c1")), QStringList{QStringLiteral("where to?")});
        QCOMPARE(store->conversations(project).size(), 2);
    }

    void anEarlierImportUnderAnotherIdIsRecognisedByItsMessages()
    {
        // Imported once under a local id, before messages were marked as Claude's.
        Conversation old;
        old.id = newId();
        old.projectId = project;
        old.title = QStringLiteral("Renamed since");
        old.createdAt = old.updatedAt = nowMs();
        store->upsertConversation(old);
        Message m;
        m.id = QStringLiteral("m1");
        m.conversationId = old.id;
        m.role = QStringLiteral("user");
        m.content = QStringLiteral("where to?");
        m.createdAt = QDateTime::fromString(kT1, Qt::ISODateWithMs).toMSecsSinceEpoch();
        store->upsertMessage(m);

        const auto out = ClaudeChatMerge::merge(
            store.get(), project, {},
            chat(QStringLiteral("c1"), QStringLiteral("Trip plan"),
                 {msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("where to?"), kT1),
                  msg(QStringLiteral("m2"), QStringLiteral("assistant"), QStringLiteral("Lisbon"), kT2)}));
        QVERIFY(!out.created);
        QCOMPARE(out.conversationId, old.id);
        QVERIFY(store->conversation(QStringLiteral("c1")).id.isEmpty()); // no second copy
        QCOMPARE(contents(old.id), (QStringList{"where to?", "Lisbon"}));
        QCOMPARE(store->conversations(project).size(), 1);
    }

    void repeatedImportsDoNotGrowTheSearchIndex()
    {
        const QJsonObject c = chat(QStringLiteral("c1"), QStringLiteral("T"),
                                   {msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("zebra crossing"), kT1),
                                    msg(QStringLiteral("m2"), QStringLiteral("assistant"), QStringLiteral("stripes"), kT2)});
        for (int i = 0; i < 4; ++i)
            ClaudeChatMerge::merge(store.get(), project, {}, c);
        QCOMPARE(store->messages(QStringLiteral("c1")).size(), 2);
        if (store->hasFts())
            QCOMPARE(rawCount(QStringLiteral("SELECT COUNT(*) FROM search_idx")), 2);
        const auto found = store->search(QStringLiteral("zebra"));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().id, QStringLiteral("c1"));
    }

    void oneChangeSignalPerImport()
    {
        QSignalSpy spy(store.get(), &Store::messagesChanged);
        ClaudeChatMerge::merge(store.get(), project, {},
                               chat(QStringLiteral("c1"), QStringLiteral("T"),
                                    {msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("a"), kT1),
                                     msg(QStringLiteral("m2"), QStringLiteral("assistant"), QStringLiteral("b"), kT2),
                                     msg(QStringLiteral("m3"), QStringLiteral("human"), QStringLiteral("c"), kT3)}));
        QCOMPARE(spy.count(), 1);
    }

    void messagesWithoutDatesKeepClaudesOrder()
    {
        ClaudeChatMerge::merge(store.get(), project, {},
                               chat(QStringLiteral("c1"), QStringLiteral("T"),
                                    {msg(QStringLiteral("z"), QStringLiteral("human"), QStringLiteral("first")),
                                     msg(QStringLiteral("a"), QStringLiteral("assistant"), QStringLiteral("second")),
                                     msg(QStringLiteral("k"), QStringLiteral("human"), QStringLiteral("third"))}));
        QCOMPARE(contents(QStringLiteral("c1")), (QStringList{"first", "second", "third"}));
    }

    void thinkingBecomesReasoningAndOtherSendersAreSkipped()
    {
        QJsonObject thinker{{QStringLiteral("uuid"), QStringLiteral("m2")},
                            {QStringLiteral("sender"), QStringLiteral("assistant")},
                            {QStringLiteral("created_at"), kT2},
                            {QStringLiteral("content"),
                             QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("thinking")},
                                                    {QStringLiteral("thinking"), QStringLiteral("hmm")}},
                                        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                    {QStringLiteral("text"), QStringLiteral("answer")}}}}};
        ClaudeChatMerge::merge(store.get(), project, {},
                               chat(QStringLiteral("c1"), QStringLiteral("T"),
                                    {msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("q"), kT1), thinker,
                                     msg(QStringLiteral("m3"), QStringLiteral("system"), QStringLiteral("ignored"), kT3),
                                     msg(QStringLiteral("m4"), QStringLiteral("human"), QString(), kT4)}));
        const auto msgs = store->messages(QStringLiteral("c1"));
        QCOMPARE(msgs.size(), 2);
        QCOMPARE(msgs.at(1).content, QStringLiteral("answer"));
        QCOMPARE(msgs.at(1).reasoning, QStringLiteral("hmm"));
    }

    // -- tool calls become short notes ------------------------------------------

    static QJsonObject tool(const QString &name, const QJsonObject &input, const QString &message = {}, const QString &integration = {})
    {
        QJsonObject o{{QStringLiteral("type"), QStringLiteral("tool_use")}, {QStringLiteral("name"), name}, {QStringLiteral("input"), input}};
        if (!message.isEmpty())
            o.insert(QStringLiteral("message"), message);
        if (!integration.isEmpty())
            o.insert(QStringLiteral("integration_name"), integration);
        return o;
    }
    static QJsonObject textBlock(const QString &t)
    {
        return {{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), t}};
    }
    QString importedReply(const QJsonArray &blocks)
    {
        const QJsonObject m{{QStringLiteral("uuid"), QStringLiteral("m1")},
                            {QStringLiteral("sender"), QStringLiteral("assistant")},
                            {QStringLiteral("created_at"), kT1},
                            {QStringLiteral("content"), blocks}};
        ClaudeChatMerge::merge(store.get(), project, {}, chat(QStringLiteral("c1"), QStringLiteral("T"), {m}));
        const auto msgs = store->messages(QStringLiteral("c1"));
        return msgs.isEmpty() ? QString() : msgs.first().content;
    }

    void toolCallsBecomeShortNotesAndResultsAreDropped()
    {
        const QJsonObject result{{QStringLiteral("type"), QStringLiteral("tool_result")},
                                 {QStringLiteral("name"), QStringLiteral("web_search")},
                                 {QStringLiteral("content"), QJsonArray{textBlock(QStringLiteral("RAW SEARCH RESULTS"))}}};
        const QString got = importedReply({textBlock(QStringLiteral("Let me look.")),
                                           tool(QStringLiteral("web_search"), {{QStringLiteral("query"), QStringLiteral("lisbon  weather\nmay")}}),
                                           result,
                                           tool(QStringLiteral("web_fetch"), {{QStringLiteral("url"), QStringLiteral("https://example.com/a")}}),
                                           textBlock(QStringLiteral("It is warm.")),
                                           textBlock(QStringLiteral("Pack light."))});
        QCOMPARE(got, QStringLiteral("Let me look.\n\n*Searched the web for “lisbon weather may”*\n\n"
                                     "*Fetched https://example.com/a*\n\nIt is warm.\nPack light."));
        QVERIFY(!got.contains(QLatin1String("RAW SEARCH RESULTS")));
    }

    void eachKindOfToolGetsAFittingNote()
    {
        const auto note = [this](const QJsonObject &block) { return importedReply({block}); };
        QCOMPARE(note(tool(QStringLiteral("create_file"), {{QStringLiteral("path"), QStringLiteral("/out/report.md")}, {QStringLiteral("file_text"), QStringLiteral("BODY")}})),
                 QStringLiteral("*Created file `/out/report.md`*"));
        QCOMPARE(note(tool(QStringLiteral("str_replace"), {{QStringLiteral("path"), QStringLiteral("a.py")}, {QStringLiteral("old_str"), QStringLiteral("x")}})),
                 QStringLiteral("*Edited file `a.py`*"));
        QCOMPARE(note(tool(QStringLiteral("view"), {{QStringLiteral("path"), QStringLiteral("a.py")}})), QStringLiteral("*Viewed `a.py`*"));
        QCOMPARE(note(tool(QStringLiteral("present_files"), {{QStringLiteral("filepaths"), QJsonArray{QStringLiteral("a.md"), QStringLiteral("b.csv")}}})),
                 QStringLiteral("*Presented `a.md`, `b.csv`*"));
        QCOMPARE(note(tool(QStringLiteral("bash_tool"), {{QStringLiteral("command"), QStringLiteral("rm -rf x")}, {QStringLiteral("description"), QStringLiteral("Clean the build folder")}})),
                 QStringLiteral("*Ran a command: Clean the build folder*"));
        QCOMPARE(note(tool(QStringLiteral("bash_tool"), {{QStringLiteral("command"), QStringLiteral("ls")}})), QStringLiteral("*Ran a command*"));
        QCOMPARE(note(tool(QStringLiteral("artifacts"), {{QStringLiteral("command"), QStringLiteral("create")}, {QStringLiteral("title"), QStringLiteral("Budget")}, {QStringLiteral("content"), QStringLiteral("<html>")}})),
                 QStringLiteral("*Created artifact “Budget”*"));
        QCOMPARE(note(tool(QStringLiteral("artifacts"), {{QStringLiteral("command"), QStringLiteral("update")}, {QStringLiteral("title"), QStringLiteral("Budget")}})),
                 QStringLiteral("*Updated artifact “Budget”*"));
        QCOMPARE(note(tool(QStringLiteral("conversation_search"), {{QStringLiteral("query"), QStringLiteral("solar")}})),
                 QStringLiteral("*Searched past chats for “solar”*"));
        QCOMPARE(note(tool(QStringLiteral("image_search"), {{QStringLiteral("query"), QStringLiteral("owls")}})),
                 QStringLiteral("*Searched for images of “owls”*"));
    }

    void otherToolsUseClaudesDescriptionOrTheirName()
    {
        const auto note = [this](const QJsonObject &block) { return importedReply({block}); };
        // Claude's own wording for the step, when it reads like words.
        QCOMPARE(note(tool(QStringLiteral("chart_display_v0"), {}, QStringLiteral("Drawing the chart"))), QStringLiteral("*Drawing the chart*"));
        // A connected tool with no usable wording.
        QCOMPARE(note(tool(QStringLiteral("pyclover:clover_metrics"), {{QStringLiteral("query"), QStringLiteral("sales")}}, QStringLiteral("clover_metrics"), QStringLiteral("pyclover"))),
                 QStringLiteral("*Used pyclover (`pyclover:clover_metrics`)*"));
        QCOMPARE(note(tool(QStringLiteral("memory_read"), {{QStringLiteral("path"), QStringLiteral("x")}})), QStringLiteral("*Used tool `memory_read`*"));
        QCOMPARE(note(tool(QString(), {})), QStringLiteral("*Used a tool*"));
        // A named tool missing the field its note needs falls back the same way.
        QCOMPARE(note(tool(QStringLiteral("web_search"), {})), QStringLiteral("*Used tool `web_search`*"));
    }

    void longDetailsAreShortened()
    {
        const QString got = importedReply({tool(QStringLiteral("web_search"), {{QStringLiteral("query"), QString(500, QLatin1Char('q'))}})});
        QVERIFY2(got.size() < 180, qPrintable(QString::number(got.size())));
        QVERIFY(got.endsWith(QStringLiteral("…”*")));
    }

    void theUnsupportedBlockPlaceholderIsNeverStored()
    {
        const QString p = QStringLiteral("This block is not supported on your current device yet.");
        QCOMPARE(importedReply({textBlock(QStringLiteral("Before.")), textBlock(p), textBlock(QStringLiteral("\n") + p + QStringLiteral("\n")),
                                textBlock(QStringLiteral("```\n") + p + QStringLiteral("\n```")), textBlock(QStringLiteral("After."))}),
                 QStringLiteral("Before.\nAfter."));
        // A reply that was nothing but placeholders is not imported as a message.
        store->deleteConversation(QStringLiteral("c1"));
        QCOMPARE(importedReply({textBlock(p)}), QString());
        QVERIFY(store->messages(QStringLiteral("c1")).isEmpty());
    }

    void reimportingReplacesStoredPlaceholdersWithNotes()
    {
        // As imported before this change: the placeholder is part of the reply.
        const QString p = QStringLiteral("This block is not supported on your current device yet.");
        QCOMPARE(importedReply({textBlock(QStringLiteral("Looking."))}), QStringLiteral("Looking."));
        Message old = store->messages(QStringLiteral("c1")).first();
        old.content = QStringLiteral("Looking.\n```\n") + p + QStringLiteral("\n```\nFound it.");
        store->upsertMessage(old);
        const QString got = importedReply({textBlock(QStringLiteral("Looking.")),
                                           tool(QStringLiteral("web_search"), {{QStringLiteral("query"), QStringLiteral("x")}}),
                                           textBlock(QStringLiteral("Found it."))});
        QCOMPARE(got, QStringLiteral("Looking.\n\n*Searched the web for “x”*\n\nFound it."));
        QCOMPARE(store->messages(QStringLiteral("c1")).size(), 1);
    }

    void aChatWithoutAnIdIsIgnored()
    {
        const auto out = ClaudeChatMerge::merge(store.get(), project, {}, QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}});
        QVERIFY(out.conversationId.isEmpty());
        QVERIFY(store->conversations(project).isEmpty());
        QVERIFY(ClaudeChatMerge::merge(nullptr, project, {}, chat(QStringLiteral("c1"), QStringLiteral("T"), {})).conversationId.isEmpty());
    }

    void aDatabaseFromBeforeThisChangeIsUpgraded()
    {
        // The messages table had no `source` column.
        store.reset();
        const QString path = dir->filePath(QStringLiteral("old.db"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE conversations (id TEXT PRIMARY KEY, project_id TEXT, title TEXT, backend_id TEXT,"
                                          "model TEXT, reasoning_effort TEXT, pinned INTEGER, created_at INTEGER, updated_at INTEGER)")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE messages (id TEXT PRIMARY KEY, conversation_id TEXT, role TEXT, content TEXT,"
                                          "reasoning TEXT, tool_calls_json TEXT, tool_call_id TEXT, created_at INTEGER)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO conversations(id,title,created_at,updated_at) VALUES('c1','Old',1,1)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO messages(id,conversation_id,role,content,created_at) VALUES('m1','c1','user','kept',5)")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));
        store = std::make_unique<Store>();
        QVERIFY(store->open(path));
        QCOMPARE(contents(QStringLiteral("c1")), QStringList{QStringLiteral("kept")});
        ClaudeChatMerge::merge(store.get(), {}, {},
                               chat(QStringLiteral("c1"), QStringLiteral("Old"),
                                    {msg(QStringLiteral("m1"), QStringLiteral("human"), QStringLiteral("kept, refreshed"), kT1),
                                     msg(QStringLiteral("m2"), QStringLiteral("assistant"), QStringLiteral("new"), kT2)}));
        QCOMPARE(contents(QStringLiteral("c1")), (QStringList{"kept, refreshed", "new"}));
        QCOMPARE(rawCount(QStringLiteral("SELECT COUNT(*) FROM messages WHERE source = 'claude'")), 2);
    }
};

QTEST_GUILESS_MAIN(TestClaudeImport)
#include "test_claude_import.moc"
