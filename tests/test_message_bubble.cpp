#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

// MessageBubble's live tool flags follow chat.toolActivityMessageId.
// The bubble is loaded from the QML sources with a stub chat, offscreen.

namespace
{

const char *kQmlFiles[] = {
    "InsightBlock.qml",
    "SelectableMarkdown.qml",
    "ImageViewer.qml",
    "TableBlock.qml",
    "CodeBlock.qml",
    "ThemedMenu.qml",
    "ThemedMenuItem.qml",
    "MenuIcon.qml",
    "MessageBubble.qml",
};

// Stand-in for the chat context property. Only the fields the bubble reads.
class StubChat : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString toolActivity READ toolActivity WRITE setToolActivity NOTIFY changed)
    Q_PROPERTY(QString toolActivityMessageId READ toolActivityMessageId
               WRITE setToolActivityMessageId NOTIFY changed)
    Q_PROPERTY(bool streaming READ streaming WRITE setStreaming NOTIFY changed)
    Q_PROPERTY(int imageRevision READ imageRevision WRITE setImageRevision NOTIFY changed)
    Q_PROPERTY(bool imageUpscaleRunning READ imageUpscaleRunning WRITE setImageUpscaleRunning
               NOTIFY changed)
    Q_PROPERTY(QString imageActivity READ imageActivity WRITE setImageActivity NOTIFY changed)
    Q_PROPERTY(QString imageActivityJobId READ imageActivityJobId WRITE setImageActivityJobId
               NOTIFY changed)
    Q_PROPERTY(int artifactsRevision READ artifactsRevision NOTIFY changed)

public:
    QString toolActivity() const { return m_toolActivity; }
    void setToolActivity(const QString &s)
    {
        m_toolActivity = s;
        emit changed();
    }
    QString toolActivityMessageId() const { return m_toolActivityMessageId; }
    void setToolActivityMessageId(const QString &s)
    {
        m_toolActivityMessageId = s;
        emit changed();
    }
    bool streaming() const { return m_streaming; }
    void setStreaming(bool v)
    {
        m_streaming = v;
        emit changed();
    }
    int imageRevision() const { return m_imageRevision; }
    void setImageRevision(int v)
    {
        m_imageRevision = v;
        emit changed();
    }
    bool imageUpscaleRunning() const { return m_imageUpscaleRunning; }
    void setImageUpscaleRunning(bool v)
    {
        m_imageUpscaleRunning = v;
        emit changed();
    }
    QString imageActivity() const { return m_imageActivity; }
    void setImageActivity(const QString &s)
    {
        m_imageActivity = s;
        emit changed();
    }
    QString imageActivityJobId() const { return m_imageActivityJobId; }
    void setImageActivityJobId(const QString &s)
    {
        m_imageActivityJobId = s;
        emit changed();
    }
    int artifactsRevision() const { return 0; }

    // Tool bubbles ask for the image record. Keyed off the tool text.
    Q_INVOKABLE QVariantMap imageForMessage(const QString &content) const
    {
        QVariantMap m;
        const QString job = content.contains(QLatin1String("job-a"))
                                ? QStringLiteral("job-a")
                            : content.contains(QLatin1String("job-b")) ? QStringLiteral("job-b")
                                                                       : QString();
        if (job.isEmpty())
        {
            return m;
        }
        m.insert(QStringLiteral("jobId"), job);
        m.insert(QStringLiteral("path"), QStringLiteral("/tmp/") + job + QStringLiteral(".png"));
        m.insert(QStringLiteral("width"), 8);
        m.insert(QStringLiteral("height"), 8);
        return m;
    }
    Q_INVOKABLE QVariantList artifactsForMessage(const QString &) const { return {}; }

signals:
    void changed();

private:
    QString m_toolActivity;
    QString m_toolActivityMessageId;
    bool m_streaming = false;
    int m_imageRevision = 0;
    bool m_imageUpscaleRunning = false;
    QString m_imageActivity;
    QString m_imageActivityJobId;
};

// Settings the bubble reads while it builds its tree.
class StubSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool showReasoning MEMBER showReasoning NOTIFY changed)
    Q_PROPERTY(bool showToolInsights MEMBER showToolInsights NOTIFY changed)
    Q_PROPERTY(bool enableArtifacts MEMBER enableArtifacts NOTIFY changed)
    Q_PROPERTY(bool showArtifactInsights MEMBER showArtifactInsights NOTIFY changed)

public:
    bool showReasoning = false;
    bool showToolInsights = false;
    bool enableArtifacts = false;
    bool showArtifactInsights = false;

signals:
    void changed();
};

// A Shammy module dir so MessageBubble can see Theme and its sibling types.
bool prepareModule(const QString &dir)
{
    const QString root = QStringLiteral(QML_SOURCE_DIR "/qml");
    const QString components = root + QStringLiteral("/components");
    if (!QDir().mkpath(dir))
    {
        return false;
    }
    if (!QFile::link(root + QStringLiteral("/Theme.qml"), dir + QStringLiteral("/Theme.qml")))
    {
        return false;
    }
    for (const char *name : kQmlFiles)
    {
        const QString from = components + QLatin1Char('/') + QLatin1String(name);
        const QString to = dir + QLatin1Char('/') + QLatin1String(name);
        if (!QFile::link(from, to))
        {
            return false;
        }
    }
    QFile qmldir(dir + QStringLiteral("/qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    const QByteArray text =
        "module Shammy\n"
        "singleton Theme 1.0 Theme.qml\n"
        "MessageBubble 1.0 MessageBubble.qml\n"
        "InsightBlock 1.0 InsightBlock.qml\n"
        "SelectableMarkdown 1.0 SelectableMarkdown.qml\n"
        "ImageViewer 1.0 ImageViewer.qml\n"
        "TableBlock 1.0 TableBlock.qml\n"
        "CodeBlock 1.0 CodeBlock.qml\n"
        "ThemedMenu 1.0 ThemedMenu.qml\n"
        "ThemedMenuItem 1.0 ThemedMenuItem.qml\n"
        "MenuIcon 1.0 MenuIcon.qml\n";
    return qmldir.write(text) == text.size();
}

const char *kDocument = R"qml(
import QtQuick
import Shammy 1.0
Item {
    width: 640
    height: 1600
    MessageBubble {
        objectName: "old"
        width: 600
        role: "assistant"
        messageId: "old"
        streaming: false
        content: "The first kitten is done."
        toolCalls: "[web_search][web_fetch][generate_image]"
    }
    MessageBubble {
        objectName: "live"
        width: 600
        role: "assistant"
        messageId: "live"
        streaming: false
        content: ""
        toolCalls: "[generate_image]"
    }
    MessageBubble {
        objectName: "search"
        width: 600
        role: "assistant"
        messageId: "search"
        streaming: false
        content: "Searched earlier."
        toolCalls: "[web_search]"
    }
    MessageBubble {
        objectName: "fetch"
        width: 600
        role: "assistant"
        messageId: "fetch"
        streaming: false
        content: "Fetched earlier."
        toolCalls: "[web_fetch]"
    }
    MessageBubble {
        objectName: "upA"
        width: 600
        role: "tool"
        messageId: "upA"
        content: "picture job-a"
    }
    MessageBubble {
        objectName: "upB"
        width: 600
        role: "tool"
        messageId: "upB"
        content: "picture job-b"
    }
}
)qml";

} // namespace

class TestMessageBubble : public QObject
{
    Q_OBJECT

private slots:
    void inactiveMessagesDoNotTakeTheLiveToolRow()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString moduleDir = dir.filePath(QStringLiteral("Shammy"));
        QVERIFY(prepareModule(moduleDir));

        StubChat chat;
        StubSettings settings;
        const QString drawing = QStringLiteral("Drawing... 11% (sampling), about 838 s left");
        chat.setToolActivity(drawing);
        chat.setToolActivityMessageId(QStringLiteral("live"));
        chat.setStreaming(true);
        chat.setImageRevision(1);
        chat.setImageUpscaleRunning(true);
        chat.setImageActivity(QStringLiteral("Drawing... 40% (sampling), about 20 s left"));
        chat.setImageActivityJobId(QStringLiteral("job-a"));

        QQmlEngine engine;
        engine.addImportPath(dir.path());
        engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
        engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
        QQmlComponent component(&engine);
        component.setData(kDocument, QUrl());
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        QObject *old = root->findChild<QObject *>(QStringLiteral("old"));
        QObject *live = root->findChild<QObject *>(QStringLiteral("live"));
        QObject *search = root->findChild<QObject *>(QStringLiteral("search"));
        QObject *fetch = root->findChild<QObject *>(QStringLiteral("fetch"));
        QObject *upA = root->findChild<QObject *>(QStringLiteral("upA"));
        QObject *upB = root->findChild<QObject *>(QStringLiteral("upB"));
        QVERIFY(old && live && search && fetch && upA && upB);

        // Stored tool calls on a finished message stay quiet.
        QCOMPARE(old->property("waitingOnTools").toBool(), false);
        QCOMPARE(old->property("searchingWeb").toBool(), false);
        QCOMPARE(old->property("fetchingPage").toBool(), false);
        QCOMPARE(search->property("searchingWeb").toBool(), false);
        QCOMPARE(fetch->property("fetchingPage").toBool(), false);

        // The message that owns the round shows the live caption.
        QCOMPARE(live->property("waitingOnTools").toBool(), true);
        QCOMPARE(live->property("searchingWeb").toBool(), false);
        QCOMPARE(live->property("waitCaption").toString(), drawing);

        // An upscale marks only the image whose job id matches.
        QCOMPARE(upA->property("upscalingThis").toBool(), true);
        QCOMPARE(upB->property("upscalingThis").toBool(), false);
        QCOMPARE(live->property("upscalingThis").toBool(), false);

        // A later web tool lights only its own message.
        chat.setToolActivityMessageId(QStringLiteral("search"));
        chat.setToolActivity(QStringLiteral("Searching the web for kittens…"));
        QCOMPARE(search->property("searchingWeb").toBool(), true);
        QCOMPARE(search->property("fetchingPage").toBool(), false);
        QCOMPARE(live->property("waitingOnTools").toBool(), false);
        QCOMPARE(old->property("searchingWeb").toBool(), false);
        QCOMPARE(fetch->property("fetchingPage").toBool(), false);

        chat.setToolActivityMessageId(QStringLiteral("fetch"));
        chat.setToolActivity(QStringLiteral("Fetching example.com…"));
        QCOMPARE(fetch->property("fetchingPage").toBool(), true);
        QCOMPARE(search->property("searchingWeb").toBool(), false);
        QCOMPARE(old->property("fetchingPage").toBool(), false);

        // No active message: every live flag is off. The upscale line stays.
        chat.setToolActivityMessageId({});
        chat.setToolActivity({});
        QCOMPARE(live->property("waitingOnTools").toBool(), false);
        QCOMPARE(search->property("searchingWeb").toBool(), false);
        QCOMPARE(fetch->property("fetchingPage").toBool(), false);
        QCOMPARE(old->property("waitingOnTools").toBool(), false);
        QCOMPARE(upA->property("upscalingThis").toBool(), true);
        QCOMPARE(upB->property("upscalingThis").toBool(), false);
    }
};

QTEST_MAIN(TestMessageBubble)
#include "test_message_bubble.moc"
