#include "openai/OpenAiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class TestOpenAiRequest : public QObject
{
    Q_OBJECT
private slots:
    void normalizeUrl()
    {
        QCOMPARE(OpenAiClient::normalizeBaseUrl("http://127.0.0.1:11434"),
                 QString("http://127.0.0.1:11434/v1"));
        QCOMPARE(OpenAiClient::normalizeBaseUrl("http://127.0.0.1:11434/"),
                 QString("http://127.0.0.1:11434/v1"));
        QCOMPARE(OpenAiClient::normalizeBaseUrl("http://127.0.0.1:11434/v1"),
                 QString("http://127.0.0.1:11434/v1"));
        QCOMPARE(OpenAiClient::normalizeBaseUrl("http://127.0.0.1:11434/v1/"),
                 QString("http://127.0.0.1:11434/v1"));
    }

    void urls()
    {
        QCOMPARE(OpenAiClient::chatCompletionsUrl("http://127.0.0.1:8080").toString(),
                 QString("http://127.0.0.1:8080/v1/chat/completions"));
        QCOMPARE(OpenAiClient::modelsUrl("http://x/v1").toString(),
                 QString("http://x/v1/models"));
    }

    void bodyAndMessages()
    {
        ChatRequest req;
        req.model = QStringLiteral("llama3.2");
        ChatMessage u;
        u.role = QStringLiteral("user");
        u.content = QStringLiteral("hi");
        req.messages.append(u);
        req.temperature = 0.2;
        req.maxTokens = 16;
        QJsonObject toolFn{{QStringLiteral("name"), QStringLiteral("sum")},
                           {QStringLiteral("parameters"), QJsonObject{}}};
        req.tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                     {QStringLiteral("function"), toolFn}});
        const QJsonObject body = QJsonDocument::fromJson(OpenAiClient::buildChatBody(req)).object();
        QCOMPARE(body.value("model").toString(), QString("llama3.2"));
        QCOMPARE(body.value("stream").toBool(), true);
        QVERIFY(body.contains("tools"));
        QCOMPARE(body.value("max_tokens").toInt(), 16);
        QCOMPARE(body.value("messages").toArray().size(), 1);
        QCOMPARE(body.value("reasoning_effort").toString(), QString("none"));
        QCOMPARE(body.value("think").toBool(), false);
        QCOMPARE(body.value("num_ctx").toInt(), 16384);
        QCOMPARE(body.value("options").toObject().value("num_ctx").toInt(), 16384);
        QVERIFY(body.value("stream_options").toObject().value("include_usage").toBool());
    }

    void nonStreamOmitsStreamOptions()
    {
        ChatRequest req;
        req.model = QStringLiteral("llama3.2");
        req.stream = false;
        ChatMessage u;
        u.role = QStringLiteral("user");
        u.content = QStringLiteral("hi");
        req.messages.append(u);
        const QJsonObject body = QJsonDocument::fromJson(OpenAiClient::buildChatBody(req)).object();
        QCOMPARE(body.value("stream").toBool(), false);
        QVERIFY(!body.contains("stream_options"));
    }

    void customContextSize()
    {
        ChatRequest req;
        req.model = QStringLiteral("llama3.2");
        req.contextSize = 32768;
        ChatMessage u;
        u.role = QStringLiteral("user");
        u.content = QStringLiteral("hi");
        req.messages.append(u);
        const QJsonObject body = QJsonDocument::fromJson(OpenAiClient::buildChatBody(req)).object();
        QCOMPARE(body.value("num_ctx").toInt(), 32768);
        QCOMPARE(body.value("options").toObject().value("num_ctx").toInt(), 32768);
    }

    void reasoningOn()
    {
        ChatRequest req;
        req.model = QStringLiteral("qwen3");
        ChatMessage u;
        u.role = QStringLiteral("user");
        u.content = QStringLiteral("hi");
        req.messages.append(u);
        req.reasoningEffort = QStringLiteral("high");
        const QJsonObject body = QJsonDocument::fromJson(OpenAiClient::buildChatBody(req)).object();
        QCOMPARE(body.value("reasoning_effort").toString(), QString("high"));
        QCOMPARE(body.value("think").toString(), QString("high"));
    }

    void imagePart()
    {
        ChatMessage u;
        u.role = QStringLiteral("user");
        u.content = QStringLiteral("what is this");
        ContentPart img;
        img.type = QStringLiteral("image");
        img.imageDataUrl = QStringLiteral("data:image/png;base64,aaa");
        u.attachments.append(img);
        const QJsonArray arr = OpenAiClient::messagesToJson({u});
        const QJsonArray parts = arr.at(0).toObject().value("content").toArray();
        QCOMPARE(parts.size(), 2);
        QCOMPARE(parts.at(1).toObject().value("type").toString(), QString("image_url"));
    }

    void defaultKey()
    {
        QCOMPARE(OpenAiClient::defaultApiKey(""), QString("ollama"));
        QCOMPARE(OpenAiClient::defaultApiKey(" sk "), QString("sk"));
    }
};

QTEST_MAIN(TestOpenAiRequest)
#include "test_openai_request.moc"
