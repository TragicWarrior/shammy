#include "mcp/JsonRpc.h"

#include <QtTest>

class TestMcpFraming : public QObject
{
    Q_OBJECT
private slots:
    void encodeRequest()
    {
        const QByteArray line = JsonRpc::encodeRequest(1, QStringLiteral("initialize"),
                                                       QJsonObject{{QStringLiteral("x"), 1}});
        QVERIFY(line.endsWith('\n'));
        QVERIFY(!line.left(line.size() - 1).contains('\n'));
        const auto m = JsonRpc::parseLine(line);
        QVERIFY(m.valid);
        QCOMPARE(m.method, QString("initialize"));
        QCOMPARE(m.id.toInt(), 1);
        QVERIFY(!m.isResponse);
    }

    void parseResponse()
    {
        const auto m = JsonRpc::parseLine(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"ok\":true}}\n");
        QVERIFY(m.valid);
        QVERIFY(m.isResponse);
        QVERIFY(m.result.toObject().value("ok").toBool());
    }

    void rejectEmbeddedNewline()
    {
        const auto m = JsonRpc::parseLine("{\"a\":1}\n{\"b\":2}\n");
        QVERIFY(!m.valid);
        QCOMPARE(m.parseError, QString("embedded newline"));
    }

    void handshakeFixture()
    {
        const QByteArray req = JsonRpc::encodeRequest(
            1, QStringLiteral("initialize"),
            QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05")}});
        QVERIFY(req.contains("initialize"));
        const auto note = JsonRpc::encodeNotification(QStringLiteral("notifications/initialized"));
        const auto parsed = JsonRpc::parseLine(note);
        QVERIFY(parsed.valid);
        QVERIFY(parsed.isNotification);
        QCOMPARE(parsed.method, QString("notifications/initialized"));
    }
};

QTEST_MAIN(TestMcpFraming)
#include "test_mcp_framing.moc"
