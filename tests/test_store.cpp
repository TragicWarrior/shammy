#include "persist/Store.h"
#include "Util.h"

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
};

QTEST_MAIN(TestStore)
#include "test_store.moc"
