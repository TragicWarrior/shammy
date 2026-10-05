#include "persist/Store.h"
#include "Util.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
QStringList raw(const QString &path, const QString &sql, const QVariantList &binds = {})
{
    QStringList out;
    const QString conn = QStringLiteral("raw-") + newId();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        if (db.open())
        {
            QSqlQuery q(db);
            q.prepare(sql);
            for (const QVariant &b : binds)
                q.addBindValue(b);
            if (q.exec())
                while (q.next())
                    out << q.value(q.record().count() - 1).toString();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return out;
}

struct SearchRig
{
    QTemporaryDir dir;
    Store store;
    bool ok = false;
    SearchRig() { ok = store.open(dir.path() + "/s.db"); }
    QString chat(const QString &title, const QStringList &messages, bool pinned = false, qint64 updated = 0)
    {
        Conversation c;
        c.id = newId();
        c.title = title;
        c.pinned = pinned;
        c.createdAt = 1;
        c.updatedAt = updated ? updated : nowMs();
        store.upsertConversation(c);
        qint64 at = 1;
        for (const QString &text : messages)
        {
            Message m;
            m.id = newId();
            m.conversationId = c.id;
            m.role = QStringLiteral("user");
            m.content = text;
            m.createdAt = at++;
            store.upsertMessage(m);
        }
        return c.id;
    }
    QStringList titles(const QString &query)
    {
        QStringList out;
        for (const Conversation &c : store.search(query))
            out << c.title;
        return out;
    }
};

} // namespace

class TestStore : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store;
        QVERIFY(store.open(dir.path() + "/t.db"));
        QVERIFY(store.backends().size() >= 2);

        Conversation c;
        c.id = newId();
        c.title = QStringLiteral("hello world");
        c.model = QStringLiteral("llama3");
        c.createdAt = c.updatedAt = nowMs();
        store.upsertConversation(c);

        Message m;
        m.id = newId();
        m.conversationId = c.id;
        m.role = QStringLiteral("user");
        m.content = QStringLiteral("the quick brown fox");
        m.createdAt = nowMs();
        store.upsertMessage(m);

        QCOMPARE(store.conversation(c.id).title, c.title);
        QCOMPARE(store.messages(c.id).size(), 1);

        const auto found = store.search(QStringLiteral("quick"));
        QVERIFY(found.size() >= 1);
        bool ok = false;
        for (const auto &x : found)
            if (x.id == c.id)
                ok = true;
        QVERIFY(ok);
    }

    void projectDescriptionAndFiles()
    {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.path() + "/proj.db"));
        Project p;
        p.id = newId();
        p.name = QStringLiteral("Shop");
        p.description = QStringLiteral("Ops for the shop");
        p.instructions = QStringLiteral("Be concise.");
        p.createdAt = p.updatedAt = nowMs();
        store.upsertProject(p);
        const Project loaded = store.project(p.id);
        QCOMPARE(loaded.name, p.name);
        QCOMPARE(loaded.description, p.description);
        QCOMPARE(loaded.instructions, p.instructions);

        p.sourceId = QStringLiteral("claude-proj-1");
        store.upsertProject(p);
        QCOMPARE(store.projectBySourceId(QStringLiteral("claude-proj-1")).id, p.id);
        QCOMPARE(store.projectByName(QStringLiteral("Shop")).id, QString());
        p.sourceId.clear();
        store.upsertProject(p);
        QCOMPARE(store.projectByName(QStringLiteral("Shop")).id, p.id);
    }

    void pinAndMoveToProject()
    {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.path() + "/p.db"));
        Project proj;
        proj.id = newId();
        proj.name = QStringLiteral("Work");
        proj.createdAt = proj.updatedAt = nowMs();
        store.upsertProject(proj);

        Conversation a;
        a.id = newId();
        a.title = QStringLiteral("plain");
        a.createdAt = a.updatedAt = nowMs();
        store.upsertConversation(a);
        Conversation b;
        b.id = newId();
        b.title = QStringLiteral("fav");
        b.pinned = true;
        b.createdAt = b.updatedAt = nowMs() + 1;
        store.upsertConversation(b);

        const auto all = store.conversations();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all.first().id, b.id);

        b.projectId = proj.id;
        store.upsertConversation(b);
        QCOMPARE(store.conversations(proj.id).size(), 1);
        QCOMPARE(store.conversations(proj.id).first().id, b.id);
        QCOMPARE(store.conversations().size(), 2);
    }

    void reasoningRoundTrip()
    {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.path() + "/r.db"));
        Conversation c;
        c.id = newId();
        c.title = QStringLiteral("think");
        c.createdAt = c.updatedAt = nowMs();
        store.upsertConversation(c);

        Message m;
        m.id = newId();
        m.conversationId = c.id;
        m.role = QStringLiteral("assistant");
        m.content = QStringLiteral("The answer is 42.");
        m.reasoning = QStringLiteral("Consider the question.\nThen reply.");
        m.createdAt = nowMs();
        store.upsertMessage(m);

        const QList<Message> loaded = store.messages(c.id);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().content, m.content);
        QCOMPARE(loaded.first().reasoning, m.reasoning);
    }

    void settingsKv()
    {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.path() + "/s.db"));
        store.setSetting("k", "v");
        QCOMPARE(store.setting("k"), QString("v"));
        QCOMPARE(store.setting("missing", "d"), QString("d"));
    }

    void deleteMessagesBatch()
    {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.path() + "/del.db"));
        Conversation c;
        c.id = newId();
        c.title = QStringLiteral("batch");
        c.createdAt = c.updatedAt = nowMs();
        store.upsertConversation(c);

        QStringList ids;
        for (int i = 0; i < 5; ++i)
        {
            Message m;
            m.id = newId();
            m.conversationId = c.id;
            m.role = QStringLiteral("user");
            m.content = QStringLiteral("row %1").arg(i);
            m.createdAt = nowMs() + i;
            store.upsertMessage(m);
            ids.append(m.id);
        }
        QCOMPARE(store.messages(c.id).size(), 5);
        store.deleteMessages(ids.mid(0, 3));
        QCOMPARE(store.messages(c.id).size(), 2);
        const auto found = store.search(QStringLiteral("row"));
        QVERIFY(found.size() >= 1);
    }

    // -- indexes ----------------------------------------------------------------

    void lookupsUseIndexes()
    {
        QTemporaryDir dir;
        Store store;
        const QString path = dir.path() + "/idx.db";
        QVERIFY(store.open(path));
        const QStringList names = raw(path, QStringLiteral("SELECT name FROM sqlite_master WHERE type='index' AND name LIKE 'idx_%' ORDER BY name"));
        QCOMPARE(names, (QStringList{"idx_artifacts_conv_ident", "idx_conversations_project", "idx_messages_conv_created", "idx_project_files_project"}));

        // The plans for the queries the app runs: by index, and (for messages) already in order.
        const auto plan = [&](const QString &sql) { return raw(path, QStringLiteral("EXPLAIN QUERY PLAN ") + sql, {QStringLiteral("x")}).join(QLatin1Char('|')); };
        const QString messages = plan(QStringLiteral("SELECT * FROM messages WHERE conversation_id = ? ORDER BY created_at"));
        QVERIFY2(messages.contains(QLatin1String("idx_messages_conv_created")), qPrintable(messages));
        QVERIFY2(!messages.contains(QLatin1String("TEMP B-TREE")), qPrintable(messages));
        const QString artifacts = plan(QStringLiteral("SELECT * FROM artifacts WHERE conversation_id = ? ORDER BY identifier, version"));
        QVERIFY2(artifacts.contains(QLatin1String("idx_artifacts_conv_ident")), qPrintable(artifacts));
        QVERIFY2(!artifacts.contains(QLatin1String("TEMP B-TREE")), qPrintable(artifacts));
        QVERIFY(plan(QStringLiteral("SELECT * FROM conversations WHERE project_id = ?")).contains(QLatin1String("idx_conversations_project")));
        QVERIFY(plan(QStringLiteral("SELECT * FROM project_files WHERE project_id = ?")).contains(QLatin1String("idx_project_files_project")));
    }

    void anExistingDatabaseGetsTheIndexesAndKeepsItsData()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/old.db";
        QString convId;
        {
            Store store;
            QVERIFY(store.open(path));
            Conversation c;
            c.id = convId = newId();
            c.title = QStringLiteral("kept");
            c.createdAt = c.updatedAt = nowMs();
            store.upsertConversation(c);
            for (int i = 0; i < 3; ++i)
            {
                Message m;
                m.id = newId();
                m.conversationId = c.id;
                m.role = QStringLiteral("user");
                m.content = QStringLiteral("line %1").arg(i);
                m.createdAt = 1000 + i;
                store.upsertMessage(m);
            }
        }
        // As a database made before this change: no indexes.
        for (const char *n : {"idx_messages_conv_created", "idx_artifacts_conv_ident", "idx_conversations_project", "idx_project_files_project"})
            raw(path, QStringLiteral("DROP INDEX %1").arg(QLatin1String(n)));
        QVERIFY(raw(path, QStringLiteral("SELECT name FROM sqlite_master WHERE type='index' AND name LIKE 'idx_%'")).isEmpty());
        Store store;
        QVERIFY(store.open(path));
        QCOMPARE(raw(path, QStringLiteral("SELECT name FROM sqlite_master WHERE type='index' AND name LIKE 'idx_%'")).size(), 4);
        const auto msgs = store.messages(convId);
        QCOMPARE(msgs.size(), 3);
        QCOMPARE(msgs.at(0).content, QStringLiteral("line 0"));
        QCOMPARE(msgs.at(2).content, QStringLiteral("line 2"));
    }

    // -- search -----------------------------------------------------------------

    void searchMatchesWordBeginningsInMessages()
    {
        SearchRig r;
        QVERIFY(r.ok);
        if (!r.store.hasFts())
            QSKIP("this SQLite has no FTS5");
        r.chat(QStringLiteral("Animals"), {QStringLiteral("the quick brown fox")});
        r.chat(QStringLiteral("Other"), {QStringLiteral("nothing to see")});
        QCOMPARE(r.titles(QStringLiteral("quick")), QStringList{QStringLiteral("Animals")});
        QCOMPARE(r.titles(QStringLiteral("qu")), QStringList{QStringLiteral("Animals")});   // found nothing by index before
        QCOMPARE(r.titles(QStringLiteral("QUICK")), QStringList{QStringLiteral("Animals")});
        QCOMPARE(r.titles(QStringLiteral("  quick  ")), QStringList{QStringLiteral("Animals")});
        QVERIFY(r.titles(QStringLiteral("zebra")).isEmpty());
    }

    void searchNeedsEveryWordAndIgnoresTheirOrder()
    {
        SearchRig r;
        QVERIFY(r.ok);
        if (!r.store.hasFts())
            QSKIP("this SQLite has no FTS5");
        r.chat(QStringLiteral("Both"), {QStringLiteral("the quick brown fox")});
        r.chat(QStringLiteral("One"), {QStringLiteral("a quick note")});
        QCOMPARE(r.titles(QStringLiteral("fox quick")), QStringList{QStringLiteral("Both")});
        QCOMPARE(r.titles(QStringLiteral("qui fo")), QStringList{QStringLiteral("Both")});
        QCOMPARE(r.titles(QStringLiteral("quick")).size(), 2);
    }

    void searchMatchesTitlesAnywhere()
    {
        SearchRig r;
        QVERIFY(r.ok);
        r.chat(QStringLiteral("Quarterly budget review"), {QStringLiteral("numbers")});
        QCOMPARE(r.titles(QStringLiteral("udget")), QStringList{QStringLiteral("Quarterly budget review")});
        QCOMPARE(r.titles(QStringLiteral("BUDGET REV")), QStringList{QStringLiteral("Quarterly budget review")});
        // % and _ are text, not wildcards.
        r.chat(QStringLiteral("100% sure"), {});
        r.chat(QStringLiteral("snake_case"), {});
        QCOMPARE(r.titles(QStringLiteral("%")), QStringList{QStringLiteral("100% sure")});
        QCOMPARE(r.titles(QStringLiteral("_")), QStringList{QStringLiteral("snake_case")});
    }

    void searchListsEachChatOncePinnedFirstThenNewest()
    {
        SearchRig r;
        QVERIFY(r.ok);
        r.chat(QStringLiteral("old apple"), {QStringLiteral("apple"), QStringLiteral("apple again"), QStringLiteral("more apple")}, false, 100);
        r.chat(QStringLiteral("new apple"), {QStringLiteral("apple")}, false, 300);
        r.chat(QStringLiteral("pinned apple"), {QStringLiteral("apple")}, true, 50);
        QCOMPARE(r.titles(QStringLiteral("apple")), (QStringList{"pinned apple", "new apple", "old apple"}));
    }

    void searchTreatsPunctuationAndOperatorsAsText()
    {
        SearchRig r;
        QVERIFY(r.ok);
        if (!r.store.hasFts())
            QSKIP("this SQLite has no FTS5");
        r.chat(QStringLiteral("Code"), {QStringLiteral("call foo-bar(baz) and say \"hello\" NOT now")});
        // None of these may be read as query syntax (they used to make the index refuse the query).
        for (const char *q : {"foo-bar", "(baz)", "\"hello\"", "NOT", "foo AND bar", "bar*", "say:", "hello OR", "^foo", "NEAR(foo bar)", "'", "\"", "a\"b"})
        {
            const QStringList got = r.titles(QString::fromLatin1(q));
            QVERIFY2(got.size() <= 1, q);
        }
        QCOMPARE(r.titles(QStringLiteral("foo-bar")), QStringList{QStringLiteral("Code")});
        QCOMPARE(r.titles(QStringLiteral("\"hello\"")), QStringList{QStringLiteral("Code")});
        QCOMPARE(r.titles(QStringLiteral("NOT now")), QStringList{QStringLiteral("Code")});
        // AND, OR and NOT are words to look for like any other.
        QCOMPARE(r.titles(QStringLiteral("foo AND bar")), QStringList{QStringLiteral("Code")});
        QVERIFY(r.titles(QStringLiteral("foo OR zebra")).isEmpty());
    }

    void ftsQueryQuotesEveryWord()
    {
        QCOMPARE(Store::ftsQuery(QStringLiteral("quick")), QStringLiteral("\"quick\"*"));
        QCOMPARE(Store::ftsQuery(QStringLiteral("  quick   fox ")), QStringLiteral("\"quick\"* \"fox\"*"));
        QCOMPARE(Store::ftsQuery(QStringLiteral("say \"hi\"")), QStringLiteral("\"say\"* \"\"\"hi\"\"\"*"));
        QCOMPARE(Store::ftsQuery(QStringLiteral("a OR b")), QStringLiteral("\"a\"* \"OR\"* \"b\"*"));
        QCOMPARE(Store::ftsQuery(QString()), QString());
    }

    void refusesEmptyConversationId()
    {
        QTemporaryDir dir;
        Store store;
        QVERIFY(store.open(dir.path() + "/empty-id.db"));
        const int before = store.conversations().size();
        Conversation c;
        c.title = QStringLiteral("New chat");
        c.model = QStringLiteral("qwen3.6:35b-a3b");
        c.createdAt = c.updatedAt = nowMs();
        store.upsertConversation(c);
        QCOMPARE(store.conversations().size(), before);
    }

    void deleteNullIdConversation()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/null-id.db";
        Store store;
        QVERIFY(store.open(path));
        Conversation keep;
        keep.id = newId();
        keep.title = QStringLiteral("keep me");
        keep.createdAt = keep.updatedAt = nowMs();
        store.upsertConversation(keep);

        const QString conn = QStringLiteral("test-null-id");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO conversations(id,title,model,pinned,created_at,updated_at) "
                "VALUES(NULL,'New chat','qwen',0,1,1)")));
            db.close();
        }
        QSqlDatabase::removeDatabase(conn);

        store.deleteConversation(QString());
        const auto all = store.conversations();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().id, keep.id);
        QVERIFY(!all.first().id.isEmpty());

        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral(
                "SELECT COUNT(*) FROM conversations WHERE id IS NULL OR id = ''")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);
            db.close();
        }
        QSqlDatabase::removeDatabase(conn);
    }
};

QTEST_MAIN(TestStore)
#include "test_store.moc"
