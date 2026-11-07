#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

// ChatView's pin and the scrollbar, off the user's display.
// A grip drag that reverses, or a follow that runs while the bar is held,
// is the bug on main. Growth above the viewport must not move the row on screen.

namespace
{

const char *kQmlFiles[] = {
    "ChatView.qml",
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

// Theme lives next to the components. The test module links both.
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
        "ChatView 1.0 ChatView.qml\n"
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
import QtQuick.Window
import Shammy 1.0
Window {
    id: win
    width: 960
    height: 720
    visible: true
    color: "#111111"
    ChatView { anchors.fill: parent }
}
)qml";

struct Row
{
    QString id;
    QString role;
    QString content;
    bool streaming = false;
    QVariantList parts;
};

class Rows : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        RoleNameRole,
        ContentRole,
        ReasoningRole,
        StreamingRole,
        PartsRole,
        ToolCallsRole,
        ToolCallIdRole,
        ErrorRole
    };

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {IdRole, "messageId"},
            {RoleNameRole, "role"},
            {ContentRole, "content"},
            {ReasoningRole, "reasoning"},
            {StreamingRole, "streaming"},
            {PartsRole, "parts"},
            {ToolCallsRole, "toolCalls"},
            {ToolCallIdRole, "toolCallId"},
            {ErrorRole, "error"},
        };
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        {
            return {};
        }
        const Row &r = m_rows.at(index.row());
        switch (role)
        {
        case IdRole:
            return r.id;
        case RoleNameRole:
            return r.role;
        case ContentRole:
            return r.content;
        case ReasoningRole:
            return QString();
        case StreamingRole:
            return r.streaming;
        case PartsRole:
            return r.parts;
        case ToolCallsRole:
            return QString();
        case ToolCallIdRole:
            return QString();
        case ErrorRole:
            return QString();
        default:
            return {};
        }
    }

    void setAll(QVector<Row> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }

    // A short user line at the end, the way a new message arrives.
    void appendShort()
    {
        Row r;
        r.id = QStringLiteral("extra-%1").arg(m_rows.size());
        r.role = QStringLiteral("user");
        r.content = QStringLiteral("A line added under the viewport.");
        QVariantMap part;
        part.insert(QStringLiteral("type"), QStringLiteral("text"));
        part.insert(QStringLiteral("text"), r.content);
        part.insert(QStringLiteral("language"), QString());
        r.parts.append(part);
        beginInsertRows({}, m_rows.size(), m_rows.size());
        m_rows.append(r);
        endInsertRows();
    }

    // Make one earlier row much taller after the view has opened.
    void grow(int row)
    {
        if (row < 0 || row >= m_rows.size())
        {
            return;
        }
        QString body;
        for (int i = 0; i < 30; ++i)
        {
            body += QStringLiteral("Late paragraph above the viewport, long enough to "
                                    "change the row height by hundreds of pixels. ");
        }
        QVariantMap part;
        part.insert(QStringLiteral("type"), QStringLiteral("text"));
        part.insert(QStringLiteral("text"), QStringLiteral("## Grown\n\n") + body);
        part.insert(QStringLiteral("language"), QString());
        m_rows[row].content = body;
        m_rows[row].parts = QVariantList{part};
        m_rows[row].streaming = false;
        const QModelIndex ix = index(row);
        emit dataChanged(ix, ix, {ContentRole, PartsRole, StreamingRole});
    }

    // Tokens on the last row. Streaming bubbles use the plain text, not parts.
    void stream(const QString &text)
    {
        if (m_rows.isEmpty())
        {
            return;
        }
        m_rows.last().streaming = true;
        m_rows.last().content += text;
        const QModelIndex ix = index(m_rows.size() - 1);
        emit dataChanged(ix, ix, {ContentRole, StreamingRole});
    }

private:
    QVector<Row> m_rows;
};

class StubChat : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Rows *messages MEMBER m_rows CONSTANT)
    Q_PROPERTY(QString toolActivity MEMBER m_tool NOTIFY changed)
    Q_PROPERTY(QString toolActivityMessageId MEMBER m_toolId NOTIFY changed)
    Q_PROPERTY(bool streaming MEMBER m_streaming NOTIFY changed)
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY changed)
    Q_PROPERTY(bool imageUpscaleRunning MEMBER m_up NOTIFY changed)
    Q_PROPERTY(int imageUpscaleScale MEMBER m_scale NOTIFY changed)
    Q_PROPERTY(QString imageActivity MEMBER m_activity NOTIFY changed)
    Q_PROPERTY(QString imageActivityJobId MEMBER m_job NOTIFY changed)
    Q_PROPERTY(int artifactsRevision MEMBER m_art NOTIFY changed)
    Q_PROPERTY(bool privateSession MEMBER m_private NOTIFY changed)
    Q_PROPERTY(QString emptyHint MEMBER m_hint NOTIFY changed)

public:
    explicit StubChat(Rows *rows, QObject *parent = nullptr)
        : QObject(parent)
        , m_rows(rows)
    {
    }

    int imageRevision() const { return m_rev; }

    void reveal()
    {
        m_reveal = true;
        ++m_rev;
        emit changed();
    }

    void send() { emit userMessageSent(); }

    Q_INVOKABLE QVariantMap imageForMessage(const QString &content) const
    {
        if (content != QLatin1String("PENDING") || !m_reveal)
        {
            return {};
        }
        QVariantMap m;
        m.insert(QStringLiteral("jobId"), QStringLiteral("pending"));
        m.insert(QStringLiteral("path"), m_path);
        m.insert(QStringLiteral("url"), QUrl::fromLocalFile(m_path));
        m.insert(QStringLiteral("width"), 1344);
        m.insert(QStringLiteral("height"), 768);
        m.insert(QStringLiteral("prompt"), QStringLiteral("pending picture"));
        m.insert(QStringLiteral("offerScale2"), false);
        m.insert(QStringLiteral("offerScale4"), false);
        m.insert(QStringLiteral("upscales"), QVariantList{});
        return m;
    }

    Q_INVOKABLE QVariantList artifactsForMessage(const QString &) const { return {}; }
    Q_INVOKABLE QString suggestedImageSaveUrl() const { return {}; }
    Q_INVOKABLE bool copyImageToClipboard(const QString &) const { return false; }
    Q_INVOKABLE bool saveImageAs(const QString &, const QUrl &) const { return false; }
    Q_INVOKABLE void upscaleImage(const QString &, int) {}
    Q_INVOKABLE void copyText(const QString &) {}
    Q_INVOKABLE void replyToSelection(const QString &) {}
    Q_INVOKABLE void editAndResend(const QString &, const QString &) {}
    Q_INVOKABLE void regenerate() {}
    Q_INVOKABLE void openArtifact(const QString &) {}

    void setPath(const QString &path) { m_path = path; }

signals:
    void changed();
    void userMessageSent();

private:
    Rows *m_rows = nullptr;
    QString m_tool;
    QString m_toolId;
    bool m_streaming = false;
    bool m_up = false;
    int m_scale = 0;
    QString m_activity;
    QString m_job;
    int m_art = 0;
    bool m_private = false;
    QString m_hint = QStringLiteral("Ask something.");
    int m_rev = 0;
    bool m_reveal = false;
    QString m_path;
};

class StubSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool showReasoning MEMBER m_r NOTIFY changed)
    Q_PROPERTY(bool showToolInsights MEMBER m_t NOTIFY changed)
    Q_PROPERTY(bool enableArtifacts MEMBER m_a NOTIFY changed)
    Q_PROPERTY(bool showArtifactInsights MEMBER m_i NOTIFY changed)
signals:
    void changed();
private:
    bool m_r = false;
    bool m_t = false;
    bool m_a = false;
    bool m_i = false;
};

QVariantMap textPart(const QString &text, const QString &type = QStringLiteral("text"),
                     const QString &language = QString())
{
    QVariantMap p;
    p.insert(QStringLiteral("type"), type);
    p.insert(QStringLiteral("text"), text);
    p.insert(QStringLiteral("language"), language);
    return p;
}

QString words(int n)
{
    QString s;
    while (s.size() < n)
    {
        s += QStringLiteral("scroll measurement word ");
    }
    return s;
}

QVector<Row> makeRows()
{
    QVector<Row> rows;
    for (int i = 0; i < 36; ++i)
    {
        Row r;
        r.id = QString::number(i);
        if (i == 2)
        {
            // An image that appears later, above wherever the reader has parked.
            r.role = QStringLiteral("tool");
            r.content = QStringLiteral("PENDING");
            rows.append(r);
            continue;
        }
        if (i % 9 == 5)
        {
            r.role = QStringLiteral("assistant");
            const QString code = QStringLiteral("int v = 0;\n")
                                 + QStringLiteral("v += 1;\n").repeated(18);
            r.content = code;
            r.parts.append(textPart(QStringLiteral("Code follows.")));
            r.parts.append(textPart(code, QStringLiteral("code"), QStringLiteral("cpp")));
        }
        else if (i % 9 == 7)
        {
            r.role = QStringLiteral("assistant");
            const QString table = QStringLiteral(
                "{\"headers\":[\"A\",\"B\",\"C\"],"
                "\"rows\":[[\"1\",\"2\",\"3\"],[\"4\",\"5\",\"6\"],"
                "[\"7\",\"8\",\"9\"],[\"10\",\"11\",\"12\"]]}");
            r.content = table;
            r.parts.append(textPart(QStringLiteral("A small table.")));
            r.parts.append(textPart(table, QStringLiteral("table"), QStringLiteral("table")));
        }
        else if (i % 2 == 0)
        {
            r.role = QStringLiteral("user");
            r.content = words(40 + (i % 5) * 30);
            r.parts.append(textPart(r.content));
        }
        else
        {
            r.role = QStringLiteral("assistant");
            r.content = QStringLiteral("## Note %1\n\n%2").arg(i).arg(words(120 + (i % 4) * 180));
            r.parts.append(textPart(r.content));
        }
        rows.append(r);
    }
    return rows;
}

QQuickItem *findNamed(QQuickItem *item, const QString &name)
{
    if (!item)
    {
        return nullptr;
    }
    if (item->objectName() == name)
    {
        return item;
    }
    const auto kids = item->childItems();
    for (QQuickItem *kid : kids)
    {
        if (QQuickItem *found = findNamed(kid, name))
        {
            return found;
        }
    }
    return nullptr;
}

struct DragResult
{
    bool pressed = false;
    int backward = 0;
    int movingSteps = 0;
    double maxAbs = 0;
    double median = 0;
    double endGap = 0;
};

} // namespace

class TestChatScroll : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void staysAtEndWhilePinned();
    void gripDragStaysMonotonic();
    void eachInputUnpinsAndThenHolds();
    void growthAboveDoesNotShiftTheViewport();
    void repinsWhenBroughtBackToTheEnd();

private:
    // Fresh conversation, pinned on the end.
    void resetView();
    bool waitReady();
    bool waitUntil(const std::function<bool()> &pred, int ms = 3000);
    QQuickItem *rowItem(int index) const;
    double contentY() const;
    double endGap() const;
    bool atEnd() const;
    bool pinned() const;
    int followRuns() const;
    void zeroFollows();
    int visibleRow(int notIndex) const;
    double sceneY(int index) const;
    DragResult dragGrip(int steps, int dy, bool append);
    void wheel(QPoint angle);
    bool dragToEnd();

    Rows m_rows;
    StubChat m_chat{&m_rows};
    StubSettings m_settings;
    QTemporaryDir m_dir;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_list = nullptr;
    QQuickItem *m_bar = nullptr;
};

void TestChatScroll::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const QString png = m_dir.filePath(QStringLiteral("pending.png"));
    QVERIFY(QImage(32, 32, QImage::Format_RGB32).save(png));
    m_chat.setPath(png);
    m_rows.setAll(makeRows());

    const QString module = m_dir.filePath(QStringLiteral("Shammy"));
    QVERIFY(prepareModule(module));
    m_engine = std::make_unique<QQmlEngine>();
    m_engine->addImportPath(m_dir.path());
    m_engine->rootContext()->setContextProperty(QStringLiteral("chat"), &m_chat);
    m_engine->rootContext()->setContextProperty(QStringLiteral("settings"), &m_settings);

    QQmlComponent component(m_engine.get());
    component.setData(kDocument, QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QObject *root = component.create();
    QVERIFY2(root, qPrintable(component.errorString()));
    m_window.reset(qobject_cast<QQuickWindow *>(root));
    QVERIFY(m_window);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    m_list = findNamed(m_window->contentItem(), QStringLiteral("chatList"));
    m_bar = findNamed(m_window->contentItem(), QStringLiteral("chatBar"));
    QVERIFY(m_list);
    QVERIFY(m_bar);
    if (!waitReady())
    {
        QQuickItem *content = m_list->property("contentItem").value<QQuickItem *>();
        qWarning("settle fail count=%d h=%f contentH=%f contentY=%f origin=%f atEnd=%d pinned=%d "
                 "follow=%d row0=%d rowMid=%d rowLast=%d kids=%lld",
                 m_rows.rowCount(),
                 m_list->height(),
                 m_list->property("contentHeight").toDouble(),
                 contentY(),
                 m_list->property("originY").toDouble(),
                 int(atEnd()),
                 int(pinned()),
                 followRuns(),
                 int(rowItem(0) != nullptr),
                 int(rowItem(m_rows.rowCount() / 2) != nullptr),
                 int(rowItem(m_rows.rowCount() - 1) != nullptr),
                 content ? static_cast<long long>(content->childItems().size()) : -1LL);
        QFAIL("the chat did not settle at the end with its rows built");
    }
}

void TestChatScroll::cleanupTestCase()
{
    m_window.reset();
    m_engine.reset();
}

void TestChatScroll::resetView()
{
    m_rows.setAll(makeRows());
    if (waitReady())
    {
        return;
    }
    QString have;
    int n = 0;
    for (int i = 0; i < m_rows.rowCount(); ++i)
    {
        if (rowItem(i))
        {
            have += QString::number(i) + QLatin1Char(',');
            ++n;
        }
    }
    qWarning("reset fail h=%f contentH=%f contentY=%f origin=%f atEnd=%d pinned=%d follow=%d "
             "rows=%d [%s] cache=%f",
             m_list->height(),
             m_list->property("contentHeight").toDouble(),
             contentY(),
             m_list->property("originY").toDouble(),
             int(atEnd()),
             int(pinned()),
             followRuns(),
             n,
             qPrintable(have),
             m_list->property("cacheBuffer").toDouble());
    QFAIL("reset did not pin the end");
}

bool TestChatScroll::waitUntil(const std::function<bool()> &pred, int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        if (pred())
        {
            return true;
        }
        QTest::qWait(20);
    }
    return pred();
}

bool TestChatScroll::waitReady()
{
    // The middle row exists only after the cache has built the whole chat.
    return waitUntil([this]() {
        return rowItem(m_rows.rowCount() / 2) && rowItem(0) && rowItem(m_rows.rowCount() - 1)
               && atEnd() && pinned();
    });
}

QQuickItem *TestChatScroll::rowItem(int index) const
{
    return findNamed(m_window->contentItem(), QStringLiteral("row-%1").arg(index));
}

double TestChatScroll::contentY() const
{
    return m_list->property("contentY").toDouble();
}

double TestChatScroll::endGap() const
{
    const double origin = m_list->property("originY").toDouble();
    const double content = m_list->property("contentHeight").toDouble();
    return origin + content - m_list->height() - contentY();
}

bool TestChatScroll::atEnd() const
{
    return m_list->property("atYEnd").toBool();
}

bool TestChatScroll::pinned() const
{
    return m_list->property("pinned").toBool();
}

int TestChatScroll::followRuns() const
{
    return m_list->property("followRuns").toInt();
}

void TestChatScroll::zeroFollows()
{
    m_list->setProperty("followRuns", 0);
}

int TestChatScroll::visibleRow(int notIndex) const
{
    for (int i = 0; i < m_rows.rowCount(); ++i)
    {
        if (i == notIndex)
        {
            continue;
        }
        QQuickItem *row = rowItem(i);
        if (!row)
        {
            continue;
        }
        const double y = row->mapToScene(QPointF(0, 0)).y();
        if (y >= 40 && y <= 460)
        {
            return i;
        }
    }
    return -1;
}

double TestChatScroll::sceneY(int index) const
{
    QQuickItem *row = rowItem(index);
    if (!row)
    {
        return -99999;
    }
    return row->mapToScene(QPointF(0, 0)).y();
}

DragResult TestChatScroll::dragGrip(int steps, int dy, bool append)
{
    DragResult result;
    auto *handle = m_bar->property("contentItem").value<QQuickItem *>();
    if (!handle)
    {
        return result;
    }
    const QPoint start = handle->mapToScene(
        QPointF(handle->width() / 2.0, handle->height() / 2.0)).toPoint();
    zeroFollows();
    QTest::mousePress(m_window.get(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::qWait(30);
    result.pressed = m_bar->property("pressed").toBool();
    double prev = contentY();
    QList<double> sizes;
    for (int i = 1; i <= steps; ++i)
    {
        if (append && (i % 4) == 0)
        {
            m_rows.appendShort();
        }
        const QPoint p(start.x(), start.y() - i * dy);
        QTest::mouseMove(m_window.get(), p);
        QTest::qWait(30);
        const double y = contentY();
        const double delta = y - prev;
        if (delta > 1.0)
        {
            ++result.backward;
        }
        const double size = std::abs(delta);
        result.maxAbs = std::max(result.maxAbs, size);
        if (size >= 1.0)
        {
            sizes.append(size);
            ++result.movingSteps;
        }
        prev = y;
    }
    const QPoint end(start.x(), start.y() - steps * dy);
    QTest::mouseRelease(m_window.get(), Qt::LeftButton, Qt::NoModifier, end);
    QTest::qWait(40);
    std::sort(sizes.begin(), sizes.end());
    if (!sizes.isEmpty())
    {
        const int mid = sizes.size() / 2;
        result.median = (sizes.size() % 2) ? sizes.at(mid)
                                            : 0.5 * (sizes.at(mid - 1) + sizes.at(mid));
    }
    result.endGap = endGap();
    return result;
}

void TestChatScroll::wheel(QPoint angle)
{
    // Over the messages, not the bar, so the list takes the wheel.
    const QPoint pos(m_window->width() / 2, m_window->height() / 2);
    QTest::wheelEvent(m_window.get(), pos, angle);
    QTest::qWait(40);
}

bool TestChatScroll::dragToEnd()
{
    auto *handle = m_bar->property("contentItem").value<QQuickItem *>();
    if (!handle)
    {
        return false;
    }
    const QPoint start = handle->mapToScene(
        QPointF(handle->width() / 2.0, handle->height() / 2.0)).toPoint();
    const QPointF barBottom = m_bar->mapToScene(QPointF(m_bar->width() / 2.0, m_bar->height() - 4));
    QTest::mousePress(m_window.get(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::qWait(20);
    // A few steps so the bar tracks the pointer down to the end of the track.
    for (int i = 1; i <= 8; ++i)
    {
        const QPoint p(start.x(), start.y() + (barBottom.y() - start.y()) * i / 8.0);
        QTest::mouseMove(m_window.get(), p);
        QTest::qWait(20);
    }
    QTest::mouseRelease(m_window.get(), Qt::LeftButton, Qt::NoModifier, barBottom.toPoint());
    return waitUntil([this]() { return atEnd() && pinned(); }, 1500);
}

void TestChatScroll::staysAtEndWhilePinned()
{
    resetView();
    const double before = contentY();
    m_rows.appendShort();
    QVERIFY(waitUntil([this]() { return atEnd(); }));
    QVERIFY2(qAbs(endGap()) <= 2.0, qPrintable(QStringLiteral("gap %1").arg(endGap())));
    QVERIFY(pinned());
    QVERIFY(contentY() >= before - 1.0);

    m_rows.grow(m_rows.rowCount() - 1);
    QVERIFY(waitUntil([this]() { return atEnd(); }));
    QVERIFY(qAbs(endGap()) <= 2.0);

    for (int i = 0; i < 40; ++i)
    {
        m_rows.stream(QStringLiteral(" token"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    QTest::qWait(60);
    QVERIFY2(atEnd(), qPrintable(QStringLiteral("stream left gap %1").arg(endGap())));
    QVERIFY(pinned());
}

void TestChatScroll::gripDragStaysMonotonic()
{
    resetView();
    // Idle. k = 2: a full cache measured a ratio of 1.00, and an append
    // changed a step by about ten percent. The old estimate jumps were 5x to 26x.
    const DragResult idle = dragGrip(14, 8, false);
    QVERIFY2(idle.pressed, "grip was not pressed");
    QVERIFY(idle.movingSteps >= 8);
    QVERIFY2(idle.backward == 0, qPrintable(QStringLiteral("idle backward %1").arg(idle.backward)));
    QVERIFY(idle.median > 1.0);
    QVERIFY2(idle.maxAbs <= idle.median * 2.0,
             qPrintable(QStringLiteral("idle max %1 median %2")
                            .arg(idle.maxAbs)
                            .arg(idle.median)));
    QVERIFY(idle.endGap > 40.0);
    QVERIFY(!pinned());
    QCOMPARE(followRuns(), 0);

    resetView();
    const DragResult live = dragGrip(14, 8, true);
    QVERIFY(live.pressed);
    QVERIFY2(live.backward == 0,
             qPrintable(QStringLiteral("append backward %1").arg(live.backward)));
    QVERIFY(live.median > 1.0);
    QVERIFY2(live.maxAbs <= live.median * 2.0,
             qPrintable(QStringLiteral("append max %1 median %2")
                            .arg(live.maxAbs)
                            .arg(live.median)));
    QCOMPARE(followRuns(), 0);
    QVERIFY(!atEnd());
}

void TestChatScroll::eachInputUnpinsAndThenHolds()
{
    // Wheel.
    resetView();
    const double wheeled = contentY();
    wheel(QPoint(0, 240));
    if (qAbs(contentY() - wheeled) < 1.0)
    {
        wheel(QPoint(0, -240));
    }
    QVERIFY2(qAbs(contentY() - wheeled) > 1.0, "wheel did not scroll");
    QVERIFY(!atEnd());
    QVERIFY(!pinned());
    const int wheelRow = visibleRow(-1);
    QVERIFY(wheelRow >= 0);
    const double wheelY = sceneY(wheelRow);
    const double wheelContent = contentY();
    m_rows.appendShort();
    m_rows.stream(QStringLiteral(" more text under the fold"));
    QTest::qWait(80);
    QVERIFY(qAbs(contentY() - wheelContent) <= 1.0);
    QVERIFY(qAbs(sceneY(wheelRow) - wheelY) <= 1.0);

    // Grip, then growth of the last row (streaming) must hold the viewport.
    resetView();
    const DragResult parked = dragGrip(10, 8, false);
    QVERIFY(parked.pressed);
    QVERIFY(!pinned());
    const int gripRow = visibleRow(-1);
    QVERIFY(gripRow >= 0);
    const double gripY = sceneY(gripRow);
    const double gripContent = contentY();
    m_rows.grow(m_rows.rowCount() - 1);
    QTest::qWait(80);
    QVERIFY2(qAbs(contentY() - gripContent) <= 1.0,
             qPrintable(QStringLiteral("contentY %1 -> %2 scene %3 -> %4 pinned %5")
                            .arg(gripContent)
                            .arg(contentY())
                            .arg(gripY)
                            .arg(sceneY(gripRow))
                            .arg(int(pinned()))));
    QVERIFY(qAbs(sceneY(gripRow) - gripY) <= 1.0);

    // Track click above the handle pages toward the start.
    resetView();
    auto *handle = m_bar->property("contentItem").value<QQuickItem *>();
    QVERIFY(handle);
    const QPoint handlePos = handle->mapToScene(
        QPointF(handle->width() / 2.0, handle->height() / 2.0)).toPoint();
    const QPoint track(handlePos.x(), std::max(8, handlePos.y() - 80));
    const double beforeTrack = contentY();
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, track);
    QTest::qWait(60);
    QVERIFY2(contentY() < beforeTrack - 1.0, "track click did not leave the end");
    QVERIFY(!pinned());
    const int trackRow = visibleRow(-1);
    QVERIFY(trackRow >= 0);
    const double trackY = sceneY(trackRow);
    const double trackContent = contentY();
    m_rows.appendShort();
    QTest::qWait(80);
    QVERIFY(qAbs(contentY() - trackContent) <= 1.0);
    QVERIFY(qAbs(sceneY(trackRow) - trackY) <= 1.0);

    // PageUp, once the list has focus.
    resetView();
    m_window->requestActivate();
    // Offscreen often never reports active. The key still goes to the list.
    (void)QTest::qWaitForWindowActive(m_window.get());
    m_list->forceActiveFocus();
    QVERIFY(m_list->hasActiveFocus());
    const double beforeKey = contentY();
    // The window click does not reach the list offscreen. Deliver it directly.
    QKeyEvent press(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_PageUp, Qt::NoModifier);
    QCoreApplication::sendEvent(m_list, &press);
    QCoreApplication::sendEvent(m_list, &release);
    QTest::qWait(60);
    QVERIFY2(contentY() < beforeKey - 1.0, "PageUp did not scroll");
    QVERIFY(!pinned());
    const int keyRow = visibleRow(-1);
    QVERIFY(keyRow >= 0);
    const double keyY = sceneY(keyRow);
    const double keyContent = contentY();
    m_rows.appendShort();
    QTest::qWait(80);
    QVERIFY(qAbs(contentY() - keyContent) <= 1.0);
    QVERIFY(qAbs(sceneY(keyRow) - keyY) <= 1.0);

    // The up chip.
    resetView();
    QQuickItem *up = findNamed(m_window->contentItem(), QStringLiteral("jumpTop"));
    QVERIFY(up);
    QVERIFY(up->isVisible());
    const QPoint upPos = up->mapToScene(QPointF(up->width() / 2.0, up->height() / 2.0)).toPoint();
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, upPos);
    QVERIFY(waitUntil([this]() { return !pinned() && contentY() <= 2.0; }));
    const int chipRow = visibleRow(2);
    QVERIFY(chipRow >= 0);
    const double chipY = sceneY(chipRow);
    const double chipContent = contentY();
    m_rows.appendShort();
    QTest::qWait(80);
    QVERIFY(qAbs(contentY() - chipContent) <= 1.0);
    QVERIFY(qAbs(sceneY(chipRow) - chipY) <= 1.0);
}

void TestChatScroll::growthAboveDoesNotShiftTheViewport()
{
    resetView();
    QVERIFY(rowItem(1));
    const DragResult parked = dragGrip(12, 8, false);
    QVERIFY(parked.endGap > 40.0);
    const int anchor = visibleRow(1);
    QVERIFY(anchor > 1);
    const double y = sceneY(anchor);
    const double before = contentY();
    // The grown row has to sit above the viewport, or this is not the case.
    QVERIFY2(sceneY(1) + rowItem(1)->height() < y, "row 1 is not above the anchor");
    m_rows.grow(1);
    QTest::qWait(120);
    QVERIFY2(qAbs(sceneY(anchor) - y) <= 1.0,
             qPrintable(QStringLiteral("text growth moved the row by %1").arg(sceneY(anchor) - y)));
    QVERIFY(qAbs(contentY() - before) > 1.0);

    const int anchor2 = visibleRow(2);
    QVERIFY(anchor2 > 2);
    const double y2 = sceneY(anchor2);
    QVERIFY(rowItem(2));
    m_chat.reveal();
    QTest::qWait(150);
    QVERIFY2(qAbs(sceneY(anchor2) - y2) <= 1.0,
             qPrintable(QStringLiteral("image growth moved the row by %1")
                            .arg(sceneY(anchor2) - y2)));
}

void TestChatScroll::repinsWhenBroughtBackToTheEnd()
{
    // Down chip.
    resetView();
    const DragResult parked = dragGrip(8, 8, false);
    QVERIFY(!parked.pressed || !pinned());
    QVERIFY(!atEnd());
    QQuickItem *down = findNamed(m_window->contentItem(), QStringLiteral("jumpBottom"));
    QVERIFY(down);
    QVERIFY(down->isVisible());
    const QPoint downPos = down->mapToScene(
        QPointF(down->width() / 2.0, down->height() / 2.0)).toPoint();
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, downPos);
    QVERIFY(waitUntil([this]() { return atEnd() && pinned(); }));
    m_rows.appendShort();
    QVERIFY(waitUntil([this]() { return atEnd(); }));
    QVERIFY(qAbs(endGap()) <= 2.0);

    // Wheel back down to the end.
    resetView();
    const double startY = contentY();
    wheel(QPoint(0, 240));
    if (contentY() > startY - 1.0)
    {
        wheel(QPoint(0, -240));
    }
    QVERIFY(contentY() < startY - 1.0);
    const QPoint back = contentY() < startY ? QPoint(0, -240) : QPoint(0, 240);
    bool reached = false;
    for (int i = 0; i < 40 && !reached; ++i)
    {
        wheel(back);
        reached = atEnd() && pinned();
    }
    QVERIFY2(reached, qPrintable(QStringLiteral("wheel did not re-pin, gap %1").arg(endGap())));
    m_rows.appendShort();
    QVERIFY(waitUntil([this]() { return atEnd() && pinned(); }));

    // Grip dragged back to the bottom of the track.
    resetView();
    QVERIFY(dragGrip(8, 8, false).endGap > 20.0);
    QVERIFY2(dragToEnd(), qPrintable(QStringLiteral("grip did not re-pin, gap %1").arg(endGap())));
    m_rows.appendShort();
    QVERIFY(waitUntil([this]() { return atEnd() && pinned(); }));

    // Send.
    resetView();
    QVERIFY(dragGrip(8, 8, false).pressed);
    QVERIFY(!pinned());
    m_chat.send();
    QVERIFY(waitUntil([this]() { return atEnd() && pinned(); }));

    // Loading another conversation.
    resetView();
    QVERIFY(dragGrip(8, 8, false).pressed);
    m_rows.setAll(makeRows());
    QVERIFY2(waitReady(), "model reset did not re-pin");
}

int main(int argc, char **argv)
{
    // ctest also sets the platform. A direct run must not open on the display.
    // The engine is created in initTestCase, so the application has to exist first.
    qunsetenv("DISPLAY");
    qunsetenv("WAYLAND_DISPLAY");
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArray("Basic"));
    QGuiApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    TestChatScroll test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "test_chat_scroll.moc"
