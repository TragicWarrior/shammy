#include "openai/ModelCaps.h"
#include "openai/OpenAiClient.h"

#include <QtTest>

class TestModelCaps : public QObject
{
    Q_OBJECT
private slots:
    void showUrlStripsV1()
    {
        QCOMPARE(ollamaShowUrl("http://127.0.0.1:11434").toString(),
                 QString("http://127.0.0.1:11434/api/show"));
        QCOMPARE(ollamaShowUrl("http://127.0.0.1:11434/v1").toString(),
                 QString("http://127.0.0.1:11434/api/show"));
    }

    void nameVision()
    {
        QVERIFY(capsFromModelId(QStringLiteral("llava:7b")).vision);
        QVERIFY(capsFromModelId(QStringLiteral("qwen2.5-vl")).vision);
        QVERIFY(!capsFromModelId(QStringLiteral("llama3.2:latest")).vision);
    }

    void nameToolsAndThinking()
    {
        QVERIFY(capsFromModelId(QStringLiteral("llama3.2")).tools);
        QVERIFY(capsFromModelId(QStringLiteral("deepseek-r1:8b")).thinking);
        QVERIFY(!capsFromModelId(QStringLiteral("gemma2:2b")).tools);
        QVERIFY(!capsFromModelId(QStringLiteral("gemma2:2b")).thinking);
    }

    void nameAudio()
    {
        QVERIFY(capsFromModelId(QStringLiteral("whisper")).audio);
        QVERIFY(!capsFromModelId(QStringLiteral("llama3.2")).audio);
    }

    void ollamaCapabilities()
    {
        const auto c = capsFromOllamaShow(
            "{\"capabilities\":[\"completion\",\"vision\",\"tools\"]}");
        QVERIFY(c.advertised);
        QVERIFY(c.vision);
        QVERIFY(c.tools);
        QVERIFY(!c.thinking);
        QVERIFY(!c.audio);
    }

    void ollamaThinkingAndProjector()
    {
        const auto t = capsFromOllamaShow("{\"capabilities\":[\"thinking\"]}");
        QVERIFY(t.thinking);
        const auto p = capsFromOllamaShow("{\"projector_info\":{\"hidden_size\":1}}");
        QVERIFY(p.advertised);
        QVERIFY(p.vision);
    }

    void ollamaErrorFallsBackEmpty()
    {
        const auto c = capsFromOllamaShow("{\"error\":\"model not found\"}");
        QVERIFY(!c.advertised);
        QVERIFY(!c.vision);
    }
};

QTEST_MAIN(TestModelCaps)
#include "test_model_caps.moc"
