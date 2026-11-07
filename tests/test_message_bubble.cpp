#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>
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
    Q_PROPERTY(int imageUpscaleScale READ imageUpscaleScale WRITE setImageUpscaleScale
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
    int imageUpscaleScale() const { return m_imageUpscaleScale; }
    void setImageUpscaleScale(int v)
    {
        m_imageUpscaleScale = v;
        emit changed();
    }
    void setImageFile(const QString &path) { m_imageFile = path; }
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
        // A full record, including the fields an empty map leaves undefined.
        if (content.contains(QLatin1String("job-img"))
            || content.contains(QLatin1String("job-up")))
        {
            return fullImage(content.contains(QLatin1String("job-up")));
        }
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
    Q_INVOKABLE QVariantList artifactsForMessage(const QString &id) const
    {
        if (id != QLatin1String("stored"))
        {
            return {};
        }
        QVariantMap p;
        p.insert(QStringLiteral("type"), QStringLiteral("artifact"));
        p.insert(QStringLiteral("text"), QStringLiteral("<p>stored body</p>"));
        p.insert(QStringLiteral("language"), QStringLiteral("html"));
        p.insert(QStringLiteral("identifier"), QStringLiteral("stored1"));
        p.insert(QStringLiteral("title"), QStringLiteral("Stored card"));
        p.insert(QStringLiteral("mime"), QStringLiteral("text/html"));
        return {p};
    }

signals:
    void changed();

private:
    // The map MessageBubble reads for a generated image and one upscale row.
    QVariantMap fullImage(bool upscale) const
    {
        QVariantMap m;
        m.insert(QStringLiteral("jobId"),
                 upscale ? QStringLiteral("job-up") : QStringLiteral("job-img"));
        m.insert(QStringLiteral("path"), m_imageFile);
        m.insert(QStringLiteral("url"), QUrl::fromLocalFile(m_imageFile).toString());
        m.insert(QStringLiteral("prompt"), QStringLiteral("a small scene"));
        m.insert(QStringLiteral("width"), 1344);
        m.insert(QStringLiteral("height"), 768);
        m.insert(QStringLiteral("offerScale2"), !upscale);
        m.insert(QStringLiteral("offerScale4"), !upscale);
        QVariantList rows;
        if (upscale)
        {
            QVariantMap row;
            row.insert(QStringLiteral("path"), m_imageFile);
            row.insert(QStringLiteral("url"), QUrl::fromLocalFile(m_imageFile).toString());
            row.insert(QStringLiteral("prompt"), QStringLiteral("upscaled"));
            row.insert(QStringLiteral("width"), 800);
            row.insert(QStringLiteral("height"), 800);
            row.insert(QStringLiteral("jobId"), QStringLiteral("job-up-2"));
            row.insert(QStringLiteral("scale"), 2);
            row.insert(QStringLiteral("label"), QStringLiteral("2× · 800×800"));
            rows.append(row);
        }
        m.insert(QStringLiteral("upscales"), rows);
        return m;
    }

    QString m_toolActivity;
    QString m_toolActivityMessageId;
    bool m_streaming = false;
    int m_imageRevision = 0;
    bool m_imageUpscaleRunning = false;
    int m_imageUpscaleScale = 0;
    QString m_imageActivity;
    QString m_imageActivityJobId;
    QString m_imageFile;
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

// Same strings the height probe laid out, so the numbers below stay meaningful.
const char *kKinds = R"qml(
import QtQuick
import Shammy 1.0
Column {
    width: 640
    spacing: 0
    MessageBubble {
        objectName: "plain"
        width: 600
        role: "user"
        messageId: "plain"
        content: "Hello from a plain bubble."
        parts: [{type: "text", text: "Hello from a plain bubble.",
                 language: "", identifier: "", title: "", mime: ""}]
    }
    MessageBubble {
        objectName: "markdown"
        width: 600
        role: "assistant"
        messageId: "markdown"
        content: "title"
        parts: [
            {type: "text",
             text: '# Title\n\nA paragraph with **bold** and enough words to wrap onto a second line in the bubble.',
             language: "", identifier: "", title: "", mime: ""},
            {type: "table",
             text: '{"headers":["A","B","C"],"rows":[["1","2","3"],["4","5","6"],["7","8","9"]]}',
             language: "table", identifier: "", title: "", mime: ""}
        ]
    }
    MessageBubble {
        objectName: "code"
        width: 600
        role: "assistant"
        messageId: "code"
        content: "code"
        parts: [{type: "code", text: "int v = 0;\nv += 1;\nv += 1;\nv += 1;\n",
                 language: "cpp", identifier: "", title: "", mime: ""}]
    }
    MessageBubble {
        objectName: "artifact"
        width: 600
        role: "assistant"
        messageId: "artifact"
        content: "art"
        parts: [{type: "artifact",
                 text: "<h1>Hello</h1>\n<p>A short artifact body for the card.</p>",
                 language: "html", identifier: "art1", title: "Demo card", mime: "text/html"}]
    }
    MessageBubble {
        objectName: "stored"
        width: 600
        role: "assistant"
        messageId: "stored"
        content: "stored"
        parts: [{type: "text", text: "See the stored card.",
                 language: "", identifier: "", title: "", mime: ""}]
    }
    MessageBubble {
        objectName: "tools"
        width: 600
        role: "assistant"
        messageId: "tools"
        content: "Used a search."
        toolCalls: "[web_search][web_fetch]"
        parts: [{type: "text", text: "Used a search.",
                 language: "", identifier: "", title: "", mime: ""}]
    }
    MessageBubble {
        objectName: "image"
        width: 600
        role: "tool"
        messageId: "image"
        content: "picture job-img"
    }
    MessageBubble {
        objectName: "upscale"
        width: 600
        role: "tool"
        messageId: "upscale"
        content: "picture job-up"
    }
    MessageBubble {
        objectName: "emptytool"
        width: 600
        role: "tool"
        messageId: "emptytool"
        content: "no picture here"
    }
    MessageBubble {
        objectName: "stream"
        width: 600
        role: "assistant"
        messageId: "stream"
        streaming: true
        content: "A streaming reply that is still growing token by token."
    }
    MessageBubble {
        objectName: "textcomp"
        width: 600
        role: "assistant"
        messageId: "textcomp"
        streaming: true
        content: ""
        parts: [{type: "text", text: "Plain markdown part while the reply is still open.",
                 language: "", identifier: "", title: "", mime: ""}]
    }
}
)qml";

// Which part the loader instantiated. The loader's height is the column space.
QString loadedKind(QObject *loaded)
{
    if (!loaded)
    {
        return {};
    }
    const QMetaObject *mo = loaded->metaObject();
    if (mo->indexOfProperty("artTitle") >= 0)
    {
        return QStringLiteral("artifact");
    }
    if (mo->indexOfProperty("json") >= 0 && mo->indexOfProperty("code") < 0)
    {
        return QStringLiteral("table");
    }
    if (mo->indexOfProperty("code") >= 0 && mo->indexOfProperty("language") >= 0)
    {
        return QStringLiteral("code");
    }
    // A QML file type is SelectableMarkdown_QMLTYPE_N, not QQuickTextEdit.
    const QString cls = QString::fromLatin1(mo->className());
    if (cls.contains(QLatin1String("TextEdit"))
        || cls.contains(QLatin1String("SelectableMarkdown"))
        || loaded->inherits("QQuickTextEdit"))
    {
        return QStringLiteral("selectable");
    }
    // textComp's root is a plain Item. Before the wrap it was a Text.
    if (cls.startsWith(QLatin1String("QQuickItem"))
        || (cls.contains(QLatin1String("QQuickText"))
            && !cls.contains(QLatin1String("TextEdit"))))
    {
        return QStringLiteral("text");
    }
    return {};
}

// Repeater delegates sit on the visual tree, not always under findChildren.
void collectItems(QQuickItem *item, QList<QQuickItem *> *out)
{
    if (!item)
    {
        return;
    }
    out->append(item);
    const auto kids = item->childItems();
    for (QQuickItem *kid : kids)
    {
        collectItems(kid, out);
    }
}

QList<double> loaderHeights(QQuickItem *bubble, const QString &kind)
{
    QList<double> out;
    QList<QQuickItem *> items;
    collectItems(bubble, &items);
    for (QQuickItem *item : items)
    {
        const QString cls = QString::fromLatin1(item->metaObject()->className());
        if (!cls.contains(QLatin1String("Loader")))
        {
            continue;
        }
        QObject *loaded = item->property("item").value<QObject *>();
        if (loadedKind(loaded) != kind)
        {
            continue;
        }
        out.append(item->height());
    }
    return out;
}

QList<double> visibleImageHeights(QQuickItem *bubble)
{
    QList<double> out;
    QList<QQuickItem *> items;
    collectItems(bubble, &items);
    for (QQuickItem *item : items)
    {
        const QString cls = QString::fromLatin1(item->metaObject()->className());
        if (!cls.contains(QLatin1String("QQuickImage")) || !item->isVisible())
        {
            continue;
        }
        if (item->height() > 1.0)
        {
            out.append(item->height());
        }
    }
    return out;
}

bool nearPx(double actual, double expected)
{
    return qAbs(actual - expected) <= 1.0;
}

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

    void partHeightsStayAndMessageBubbleIsQuiet()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString moduleDir = dir.filePath(QStringLiteral("Shammy"));
        QVERIFY(prepareModule(moduleDir));

        QImage img(32, 32, QImage::Format_RGB32);
        img.fill(Qt::darkBlue);
        const QString png = dir.filePath(QStringLiteral("img.png"));
        QImageWriter writer(png, "png");
        QVERIFY(writer.write(img));

        StubChat chat;
        chat.setImageFile(png);
        chat.setImageRevision(1);
        StubSettings settings;
        settings.enableArtifacts = true;

        // Only MessageBubble.qml. Font notices on macOS CI must not fail this.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("MessageBubble\\.qml")));

        QQmlEngine engine;
        engine.addImportPath(dir.path());
        engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
        engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
        QQmlComponent component(&engine);
        component.setData(QByteArray(kKinds), QUrl());
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto *column = qobject_cast<QQuickItem *>(root.get());
        QVERIFY(column);
        QQuickWindow window;
        window.resize(640, 4000);
        column->setParentItem(window.contentItem());
        window.show();
        QTest::qWait(200);

        auto bubble = [&](const char *name) {
            return root->findChild<QQuickItem *>(QLatin1String(name));
        };
        QQuickItem *plain = bubble("plain");
        QQuickItem *markdown = bubble("markdown");
        QQuickItem *code = bubble("code");
        QQuickItem *artifact = bubble("artifact");
        QQuickItem *stored = bubble("stored");
        QQuickItem *tools = bubble("tools");
        QQuickItem *image = bubble("image");
        QQuickItem *upscale = bubble("upscale");
        QQuickItem *emptyTool = bubble("emptytool");
        QQuickItem *stream = bubble("stream");
        QQuickItem *textComp = bubble("textcomp");
        QVERIFY(plain && markdown && code && artifact && stored && tools);
        QVERIFY(image && upscale && emptyTool && stream && textComp);

        // Heights measured offscreen on the bubble before this fix.
        const auto plainText = loaderHeights(plain, QStringLiteral("selectable"));
        QCOMPARE(plainText.size(), 1);
        QVERIFY2(nearPx(plainText.at(0), 21.0), qPrintable(QString::number(plainText.at(0))));
        QVERIFY(nearPx(plain->height(), 66.0));

        const auto mdText = loaderHeights(markdown, QStringLiteral("selectable"));
        const auto mdTable = loaderHeights(markdown, QStringLiteral("table"));
        QCOMPARE(mdText.size(), 1);
        QCOMPARE(mdTable.size(), 1);
        QVERIFY(nearPx(mdText.at(0), 62.0));
        QVERIFY(nearPx(mdTable.at(0), 122.0));
        QVERIFY(nearPx(markdown->height(), 253.0));

        const auto codeH = loaderHeights(code, QStringLiteral("code"));
        QCOMPARE(codeH.size(), 1);
        QVERIFY(nearPx(codeH.at(0), 125.0));
        QVERIFY(nearPx(code->height(), 186.0));

        const auto artH = loaderHeights(artifact, QStringLiteral("artifact"));
        QCOMPARE(artH.size(), 1);
        QVERIFY(nearPx(artH.at(0), 63.0));
        QVERIFY(nearPx(artifact->height(), 124.0));

        const auto storedText = loaderHeights(stored, QStringLiteral("selectable"));
        const auto storedArt = loaderHeights(stored, QStringLiteral("artifact"));
        QCOMPARE(storedText.size(), 1);
        QCOMPARE(storedArt.size(), 1);
        QVERIFY(nearPx(storedText.at(0), 21.0));
        QVERIFY(nearPx(storedArt.at(0), 63.0));
        QVERIFY(nearPx(stored->height(), 153.0));

        const auto toolText = loaderHeights(tools, QStringLiteral("selectable"));
        QCOMPARE(toolText.size(), 1);
        QVERIFY(nearPx(toolText.at(0), 21.0));
        QVERIFY(nearPx(tools->height(), 107.0));

        const auto imageH = visibleImageHeights(image);
        QCOMPARE(imageH.size(), 1);
        QVERIFY(nearPx(imageH.at(0), 343.0));
        QVERIFY(nearPx(image->height(), 414.0));

        const auto upH = visibleImageHeights(upscale);
        QCOMPARE(upH.size(), 2);
        QVERIFY(nearPx(upH.at(0), 343.0));
        QVERIFY(nearPx(upH.at(1), 600.0));
        QVERIFY(nearPx(upscale->height(), 1066.0));

        QCOMPARE(visibleImageHeights(emptyTool).size(), 0);
        QVERIFY(nearPx(emptyTool->height(), 17.0));

        const auto plainPart = loaderHeights(textComp, QStringLiteral("text"));
        QCOMPARE(plainPart.size(), 1);
        QVERIFY(nearPx(plainPart.at(0), 21.44));
        QVERIFY(nearPx(textComp->height(), 83.44));

        QVERIFY(nearPx(stream->height(), 92.45));
        for (int i = 0; i < 8; ++i)
        {
            stream->setProperty("content",
                                stream->property("content").toString() + QStringLiteral(" token"));
            QTest::qWait(30);
        }
        QVERIFY(nearPx(stream->height(), 122.90));
    }
};

QTEST_MAIN(TestMessageBubble)
#include "test_message_bubble.moc"
