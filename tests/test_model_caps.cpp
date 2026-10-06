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

    void psUrlStripsV1()
    {
        QCOMPARE(ollamaPsUrl("http://127.0.0.1:11434/v1").toString(), QString("http://127.0.0.1:11434/api/ps"));
        QCOMPARE(ollamaPsUrl("http://127.0.0.1:11434/").toString(), QString("http://127.0.0.1:11434/api/ps"));
    }

    void loadedContextIsReadForTheRightModel()
    {
        const QByteArray ps = R"({"models":[
            {"name":"gemma4:26b-a4b","model":"gemma4:26b-a4b","context_length":4096},
            {"name":"batiai/qwen3.6-35b:iq3","model":"batiai/qwen3.6-35b:iq3","context_length":131072},
            {"name":"llama3.2:latest","model":"llama3.2:latest","context_length":8192},
            {"name":"old:latest","model":"old:latest"}]})";
        QCOMPARE(loadedContextFromOllamaPs(ps, QStringLiteral("gemma4:26b-a4b")), 4096);
        QCOMPARE(loadedContextFromOllamaPs(ps, QStringLiteral("batiai/qwen3.6-35b:iq3")), 131072);
        // No tag means ":latest", in either direction, and case does not matter.
        QCOMPARE(loadedContextFromOllamaPs(ps, QStringLiteral("llama3.2")), 8192);
        QCOMPARE(loadedContextFromOllamaPs(ps, QStringLiteral("LLAMA3.2:latest")), 8192);
        // Not loaded, another tag of the same model, an older Ollama without the field, not JSON.
        QCOMPARE(loadedContextFromOllamaPs(ps, QStringLiteral("mistral")), 0);
        QCOMPARE(loadedContextFromOllamaPs(ps, QStringLiteral("gemma4:12b")), 0);
        QCOMPARE(loadedContextFromOllamaPs(ps, QStringLiteral("old")), 0);
        QCOMPARE(loadedContextFromOllamaPs(ps, QString()), 0);
        QCOMPARE(loadedContextFromOllamaPs("<html>404</html>", QStringLiteral("llama3.2")), 0);
        QCOMPARE(loadedContextFromOllamaPs("{\"models\":[]}", QStringLiteral("llama3.2")), 0);
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
