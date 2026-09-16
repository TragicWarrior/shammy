#include "persist/Store.h"
#include "Util.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

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
