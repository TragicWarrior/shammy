#include "mcp/McpClient.h"
#include "mcp/McpHost.h"

#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

// Tool calls that are still waiting when their server goes away must be failed
// with a reason instead of being dropped, otherwise the chat waits forever.
//
// The servers here are throwaway /bin/sh scripts speaking just enough MCP:
// they answer initialize and tools/list, read one tools/call, and then do
// whatever the test needs (exit, hang, reply late, reply).

namespace
{

struct CallLog
{
    int calls = 0;
    QString error;
    QJsonValue result;
};

McpServerConfig fakeServer(const QString &name, const QString &afterCall)
{
    static const QString head = QStringLiteral(
        "read init; printf '%s\\n' '{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}'; "
        "read note; read list; "
        "printf '%s\\n' '{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"tools\":[{\"name\":\"slow\"}]}}'; "
        "read call; ");
    McpServerConfig c;
    c.name = name;
    c.command = QStringLiteral("/bin/sh");
    c.args = {QStringLiteral("-c"), head + afterCall};
    return c;
}

const QString kReply = QStringLiteral(
    "id=$(printf '%s' \"$call\" | sed 's/.*\"id\":\\([0-9]*\\).*/\\1/'); "
    "printf '{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"ok\"}]}}\\n' \"$id\"; "
    "exec sleep 30");

void call(McpClient &client, CallLog *log)
{
    client.callTool(QStringLiteral("slow"), {},
                    [log](const QJsonValue &result, const QString &error)
                    {
                        ++log->calls;
                        log->error = error;
                        log->result = result;
                    });
}

} // namespace

class TestMcpClient : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!QFile::exists(QStringLiteral("/bin/sh")))
            QSKIP("needs /bin/sh to fake an MCP server");
    }

    void replyIsDelivered()
    {
        McpClient client(fakeServer(QStringLiteral("t"), kReply));
        client.start();
        QTRY_VERIFY(client.state() == McpClient::State::Connected);
        CallLog log;
        call(client, &log);
        QTRY_COMPARE(log.calls, 1);
        QVERIFY2(log.error.isEmpty(), qPrintable(log.error));
        QVERIFY(log.result.isObject());
    }

    void pendingCallFailsWhenServerExits()
    {
        McpClient client(fakeServer(QStringLiteral("t"), QStringLiteral("exit 0")));
        client.start();
        QTRY_VERIFY(client.state() == McpClient::State::Connected);
        CallLog log;
        call(client, &log);
        QTRY_COMPARE(log.calls, 1);
        QVERIFY2(log.error.contains(QLatin1String("exited")), qPrintable(log.error));
    }

    void pendingCallFailsOnStop()
    {
        McpClient client(fakeServer(QStringLiteral("t"), QStringLiteral("exec sleep 30")));
        client.start();
        QTRY_VERIFY(client.state() == McpClient::State::Connected);
        CallLog log;
        call(client, &log);
        client.stop();
        QTRY_COMPARE(log.calls, 1);
        QVERIFY2(log.error.contains(QLatin1String("stopped")), qPrintable(log.error));
    }

    void silentServerTimesOut()
    {
        McpClient client(fakeServer(QStringLiteral("t"), QStringLiteral("exec sleep 30")));
        client.setCallTimeoutMs(200);
        client.start();
        QTRY_VERIFY(client.state() == McpClient::State::Connected);
        CallLog log;
        call(client, &log);
        QCOMPARE(log.calls, 0);
        QTRY_COMPARE_WITH_TIMEOUT(log.calls, 1, 3000);
        QVERIFY2(log.error.contains(QLatin1String("timed out")), qPrintable(log.error));
        client.stop();
    }

    void lateReplyAfterTimeoutIsIgnored()
    {
        // Reply arrives after the call has already been failed: it must not
        // invoke the callback a second time.
        McpClient client(fakeServer(QStringLiteral("t"), QStringLiteral("sleep 0.6; ") + kReply));
        client.setCallTimeoutMs(150);
        client.start();
        QTRY_VERIFY(client.state() == McpClient::State::Connected);
        CallLog log;
        call(client, &log);
        QTRY_COMPARE_WITH_TIMEOUT(log.calls, 1, 3000);
        QVERIFY(log.error.contains(QLatin1String("timed out")));
        QTest::qWait(900);
        QCOMPARE(log.calls, 1);
        client.stop();
    }

    void timeoutDoesNotFireAfterReply()
    {
        McpClient client(fakeServer(QStringLiteral("t"), kReply));
        client.setCallTimeoutMs(250);
        client.start();
        QTRY_VERIFY(client.state() == McpClient::State::Connected);
        CallLog log;
        call(client, &log);
        QTRY_COMPARE(log.calls, 1);
        QVERIFY(log.error.isEmpty());
        QTest::qWait(500);
        QCOMPARE(log.calls, 1);
        QVERIFY(log.error.isEmpty());
        client.stop();
    }

    void rebuildingHostFailsInFlightCalls()
    {
        // Editing MCP settings replaces every client. A call in flight on one of
        // them has to be failed, not lost with the client that carried it.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        McpHost host;
        host.setConfigPath(dir.filePath(QStringLiteral("mcp.json")));
        host.replaceConfigs({fakeServer(QStringLiteral("s"), QStringLiteral("exec sleep 30"))});
        QTRY_COMPARE(host.connectedCount(), 1);

        CallLog log;
        host.callTool(QStringLiteral("slow"), {},
                      [&log](const QJsonValue &result, const QString &error)
                      {
                          ++log.calls;
                          log.error = error;
                          log.result = result;
                      });
        host.replaceConfigs({fakeServer(QStringLiteral("s2"), QStringLiteral("exec sleep 30"))});
        QTRY_COMPARE(log.calls, 1);
        QVERIFY2(log.error.contains(QLatin1String("stopped")), qPrintable(log.error));
        host.stopAll();
    }
};

QTEST_GUILESS_MAIN(TestMcpClient)
#include "test_mcp_client.moc"
