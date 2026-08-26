#include "openai/SseParser.h"

#include <QtTest>

class TestSseParser : public QObject
{
    Q_OBJECT
private slots:
    void contentChunks()
    {
        SseParser p;
        const QByteArray a =
            "data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n";
        const QByteArray b =
            "data: {\"choices\":[{\"delta\":{\"content\":\"lo\"}}]}\n\n"
            "data: [DONE]\n\n";
        auto e1 = p.feed(a);
        QCOMPARE(e1.size(), 1);
        QCOMPARE(e1[0].contentDelta, QString("Hel"));
        auto e2 = p.feed(b);
        QCOMPARE(e2.size(), 2);
        QCOMPARE(e2[0].contentDelta, QString("lo"));
        QVERIFY(e2[1].done);
    }

    void splitAcrossPackets()
    {
        SseParser p;
        auto e1 = p.feed("data: {\"choices\":[{\"delta\":{\"con");
        QVERIFY(e1.isEmpty());
        auto e2 = p.feed("tent\":\"x\"}}]}\n");
        QCOMPARE(e2.size(), 1);
        QCOMPARE(e2[0].contentDelta, QString("x"));
    }

    void toolCallDeltas()
    {
        SseParser p;
        auto e = p.feed(
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":"
            "[{\"index\":0,\"id\":\"call_1\",\"function\":{\"name\":\"foo\",\"arguments\":\"{\\\"a\\\"\"}}]}}]}\n");
        QCOMPARE(e.size(), 1);
        QCOMPARE(e[0].toolCalls.size(), 1);
        QCOMPARE(e[0].toolCalls[0].name, QString("foo"));
        QCOMPARE(e[0].toolCalls[0].id, QString("call_1"));
        QVERIFY(e[0].toolCalls[0].argumentsDelta.contains("a"));
    }

    void reasoning()
    {
        SseParser p;
        auto e = p.feed(
            "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"hmm\"}}]}\n");
        QCOMPARE(e.size(), 1);
        QCOMPARE(e[0].reasoningDelta, QString("hmm"));
    }

    void ignoreCommentsAndGarbage()
    {
        SseParser p;
        auto e = p.feed(": keep-alive\nnot-sse\ndata: [DONE]\n");
        QCOMPARE(e.size(), 1);
        QVERIFY(e[0].done);
    }

    void usageChunk()
    {
        SseParser p;
        auto e = p.feed(
            "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":12,\"completion_tokens\":8,"
            "\"total_tokens\":20}}\n");
        QCOMPARE(e.size(), 1);
        QVERIFY(e[0].hasUsage);
        QCOMPARE(e[0].promptTokens, 12);
        QCOMPARE(e[0].completionTokens, 8);
        QCOMPARE(e[0].totalTokens, 20);
    }

    void rawJsonLine()
    {
        SseParser p;
        auto e = p.feed("{\"choices\":[{\"delta\":{\"content\":\"z\"}}]}\n");
        QCOMPARE(e.size(), 1);
        QCOMPARE(e[0].contentDelta, QString("z"));
    }
};

QTEST_MAIN(TestSseParser)
#include "test_sseparser.moc"
