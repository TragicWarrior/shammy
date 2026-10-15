#include "openai/OllamaNative.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class TestOllamaNative : public QObject
{
    Q_OBJECT

    static QJsonObject body(const ChatRequest &r)
    {
        return QJsonDocument::fromJson(OllamaNative::buildChatBody(r)).object();
    }

private slots:
    void urls()
    {
        QCOMPARE(OllamaNative::chatUrl(QStringLiteral("http://h:11434/v1")).toString(), QStringLiteral("http://h:11434/api/chat"));
        QCOMPARE(OllamaNative::chatUrl(QStringLiteral("http://h:11434/")).toString(), QStringLiteral("http://h:11434/api/chat"));
        QCOMPARE(OllamaNative::versionUrl(QStringLiteral("http://h:11434")).toString(), QStringLiteral("http://h:11434/api/version"));
    }

    void optionsCarryTheSettings()
    {
        ChatRequest r;
        r.model = QStringLiteral("m");
        r.contextSize = 65536;
        r.temperature = 0.3;
        r.topP = 0.9;
        r.maxTokens = 500;
        const QJsonObject o = body(r).value(QStringLiteral("options")).toObject();
        QCOMPARE(o.value(QStringLiteral("num_ctx")).toInt(), 65536);
        QCOMPARE(o.value(QStringLiteral("temperature")).toDouble(), 0.3);
        QCOMPARE(o.value(QStringLiteral("top_p")).toDouble(), 0.9);
        QCOMPARE(o.value(QStringLiteral("num_predict")).toInt(), 500);
        r.topP = 1.0;
        r.maxTokens = 0;
        const QJsonObject o2 = body(r).value(QStringLiteral("options")).toObject();
        QVERIFY(!o2.contains(QStringLiteral("top_p")));
        QVERIFY(!o2.contains(QStringLiteral("num_predict")));
        QVERIFY(!body(r).contains(QStringLiteral("tools")));
    }

    void thinkingIsOnOffOrALevelForGptOss()
    {
        ChatRequest r;
        r.model = QStringLiteral("qwen3:8b");
        QCOMPARE(body(r).value(QStringLiteral("think")), QJsonValue(false));
        r.reasoningEffort = QStringLiteral("none");
        QCOMPARE(body(r).value(QStringLiteral("think")), QJsonValue(false));
        r.reasoningEffort = QStringLiteral("high");
        QCOMPARE(body(r).value(QStringLiteral("think")), QJsonValue(true));
        r.model = QStringLiteral("gpt-oss:20b");
        QCOMPARE(body(r).value(QStringLiteral("think")), QJsonValue(QStringLiteral("high")));
    }

    void messagesInOllamasShape()
    {
        QVector<ChatMessage> msgs;
        ChatMessage u;
        u.role = QStringLiteral("user");
        u.content = QStringLiteral("look");
        ContentPart img;
        img.type = QStringLiteral("image");
        img.imageDataUrl = QStringLiteral("data:image/png;base64,QUJD");
        u.attachments << img;
        msgs << u;
        ChatMessage a;
        a.role = QStringLiteral("assistant");
        a.toolCallsJson = QStringLiteral("[{\"id\":\"c1\",\"type\":\"function\",\"function\":{\"name\":\"lookup\",\"arguments\":\"{\\\"q\\\":\\\"x\\\"}\"}},"
                                         "{\"id\":\"c2\",\"type\":\"function\",\"function\":{\"name\":\"other\",\"arguments\":\"not json\"}}]");
        msgs << a;
        ChatMessage t1;
        t1.role = QStringLiteral("tool");
        t1.toolCallId = QStringLiteral("c2");
        t1.content = QStringLiteral("[other]\nresult two");
        msgs << t1;
        ChatMessage t2;
        t2.role = QStringLiteral("tool");
        t2.toolCallId = QStringLiteral("unknown");
        t2.content = QStringLiteral("orphan");
        msgs << t2;

        const QJsonArray out = OllamaNative::messagesToJson(msgs);
        QCOMPARE(out.size(), 4);
        QCOMPARE(out.at(0).toObject().value(QStringLiteral("images")).toArray(), QJsonArray{QStringLiteral("QUJD")});
        QCOMPARE(out.at(0).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("look"));
        const QJsonArray calls = out.at(1).toObject().value(QStringLiteral("tool_calls")).toArray();
        QCOMPARE(calls.size(), 2);
        const QJsonObject fn0 = calls.at(0).toObject().value(QStringLiteral("function")).toObject();
        QCOMPARE(fn0.value(QStringLiteral("arguments")).toObject().value(QStringLiteral("q")).toString(), QStringLiteral("x"));
        QCOMPARE(fn0.value(QStringLiteral("index")).toInt(), 0);
        QVERIFY(calls.at(1).toObject().value(QStringLiteral("function")).toObject().value(QStringLiteral("arguments")).isObject());
        QCOMPARE(out.at(2).toObject().value(QStringLiteral("tool_name")).toString(), QStringLiteral("other"));
        QCOMPARE(out.at(2).toObject().value(QStringLiteral("tool_call_id")).toString(), QStringLiteral("c2"));
        QVERIFY(!out.at(3).toObject().contains(QStringLiteral("tool_name")));
        QVERIFY(!out.at(1).toObject().contains(QStringLiteral("images")));
    }

    void streamReadsContentThinkingToolsAndCounts()
    {
        OllamaNative::StreamParser p;
        QVector<SseParser::Event> evs;
        // Split mid-line, and blank lines between.
        evs += p.feed("{\"message\":{\"role\":\"assistant\",\"thinking\":\"hmm\"},\"done\":false}" "\n\n" "{\"message\":{\"content\":\"Hel");
        evs += p.feed("lo\"},\"done\":false}" "\n");
        evs += p.feed("{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"t9\",\"function\":{\"index\":0,\"name\":\"get\",\"arguments\":{\"city\":\"Paris\"}}},"
                      "{\"function\":{\"name\":\"two\",\"arguments\":\"{}\"}}]},\"done\":false}" "\n");
        evs += p.feed("{\"done\":true,\"done_reason\":\"stop\",\"prompt_eval_count\":30,\"eval_count\":12}");
        evs += p.flush();
        QCOMPARE(evs.size(), 4);
        QCOMPARE(evs.at(0).reasoningDelta, QStringLiteral("hmm"));
        QCOMPARE(evs.at(1).contentDelta, QStringLiteral("Hello"));
        QCOMPARE(evs.at(2).toolCalls.size(), 2);
        QCOMPARE(evs.at(2).toolCalls.at(0).id, QStringLiteral("t9"));
        QCOMPARE(evs.at(2).toolCalls.at(0).argumentsDelta, QStringLiteral("{\"city\":\"Paris\"}"));
        QCOMPARE(evs.at(2).toolCalls.at(1).index, 1); // no index given: the next one
        QCOMPARE(evs.at(2).toolCalls.at(1).argumentsDelta, QStringLiteral("{}"));
        QVERIFY(evs.at(3).done);
        QCOMPARE(evs.at(3).finishReason, QStringLiteral("tool_calls")); // "stop" after tool calls
        QVERIFY(evs.at(3).hasUsage);
        QCOMPARE(evs.at(3).totalTokens, 42);
    }

    void streamReportsErrorsAndLength()
    {
        OllamaNative::StreamParser p;
        auto evs = p.feed("{\"error\":\"m does not support tools\"}" "\n");
        QCOMPARE(evs.size(), 1);
        QCOMPARE(evs.at(0).error, QStringLiteral("m does not support tools"));
        p.reset();
        evs = p.feed("{\"done\":true,\"done_reason\":\"length\"}" "\n" "not json\n");
        QCOMPARE(evs.size(), 1);
        QCOMPARE(evs.at(0).finishReason, QStringLiteral("length"));
        QVERIFY(!evs.at(0).hasUsage);
    }
};

QTEST_GUILESS_MAIN(TestOllamaNative)
#include "test_ollama_native.moc"
