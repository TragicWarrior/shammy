#include <QAbstractListModel>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

// The sidebar × of a row that is not generating must stay shown while the
// pointer rests on it. The other row's spinner keeps the window dirty, which
// used to flip that × on and off.

namespace
{

const char *kQmlFiles[] = {
    "Sidebar.qml",
    "ThemedMenu.qml",
    "ThemedMenuItem.qml",
    "MenuIcon.qml",
};

// One chat row in the stub list.
struct ConvRow
{
    QString id;
    QString title;
    bool pinned = false;
};

// Conversations and favorites. Roles match the chat-row delegate.
class ConvModel : public QAbstractListModel
{
public:
    QList<ConvRow> rows;

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : rows.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows.size())
        {
            return {};
        }
        const ConvRow &row = rows.at(index.row());
        switch (role)
        {
        case Qt::UserRole + 1:
            return row.id;
        case Qt::UserRole + 2:
            return row.title;
        case Qt::UserRole + 3:
            return row.pinned;
        case Qt::UserRole + 4:
            return QString();
        case Qt::UserRole + 5:
            return QString();
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {Qt::UserRole + 1, "conversationId"},
            {Qt::UserRole + 2, "title"},
            {Qt::UserRole + 3, "pinned"},
            {Qt::UserRole + 4, "modelName"},
            {Qt::UserRole + 5, "projectId"},
        };
    }
};

// Empty project list. The delegate asks for these roles if a row exists.
class ProjectModel : public QAbstractListModel
{
public:
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 0;
    }

    QVariant data(const QModelIndex &, int) const override
    {
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {Qt::UserRole + 1, "projectId"},
            {Qt::UserRole + 2, "name"},
        };
    }
};

// Stand-in for the chat context property. Only what Sidebar.qml reads.
class StubChat : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString conversationId MEMBER conversationId NOTIFY changed)
    Q_PROPERTY(QString generatingConversationId READ generatingConversationId
               WRITE setGeneratingConversationId NOTIFY changed)
    Q_PROPERTY(bool privateSession MEMBER privateSession NOTIFY changed)
    Q_PROPERTY(QString searchQuery MEMBER searchQuery NOTIFY changed)
    Q_PROPERTY(QObject *conversations MEMBER conversations NOTIFY changed)
    Q_PROPERTY(QObject *favorites MEMBER favorites NOTIFY changed)

public:
    QString conversationId = QStringLiteral("gen");
    bool privateSession = false;
    QString searchQuery;
    QObject *conversations = nullptr;
    QObject *favorites = nullptr;

    QString generatingConversationId() const
    {
        return m_generatingConversationId;
    }

    void setGeneratingConversationId(const QString &id)
    {
        // The row spinner binds to this, so the notify has to fire.
        if (m_generatingConversationId == id)
        {
            return;
        }
        m_generatingConversationId = id;
        emit changed();
    }

    Q_INVOKABLE void newChat() {}
    Q_INVOKABLE void newPrivateChat() {}
    Q_INVOKABLE void openConversation(const QString &) {}
    Q_INVOKABLE void togglePin(const QString &) {}
    Q_INVOKABLE void deleteConversation(const QString &) {}
    Q_INVOKABLE void renameConversation(const QString &, const QString &) {}
    Q_INVOKABLE void moveToProject(const QString &, const QString &) {}

signals:
    void changed();

private:
    QString m_generatingConversationId;
};

// Navigator flags Sidebar persists. The Connections handler names the signal.
class StubSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool rememberNavigatorState MEMBER rememberNavigatorState
               NOTIFY rememberNavigatorStateChanged)
    Q_PROPERTY(bool navProjectsOpen MEMBER navProjectsOpen NOTIFY changed)
    Q_PROPERTY(bool navFavoritesOpen MEMBER navFavoritesOpen NOTIFY changed)
    Q_PROPERTY(bool navChatsOpen MEMBER navChatsOpen NOTIFY changed)

public:
    bool rememberNavigatorState = true;
    bool navProjectsOpen = false;
    bool navFavoritesOpen = false;
    bool navChatsOpen = true;

signals:
    void rememberNavigatorStateChanged();
    void changed();
};

// Stand-in for the projects context property.
class StubProjects : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *projects MEMBER projects NOTIFY changed)
    Q_PROPERTY(QString currentProjectId MEMBER currentProjectId NOTIFY changed)

public:
    QObject *projects = nullptr;
    QString currentProjectId;

    Q_INVOKABLE void goHome() {}
    Q_INVOKABLE void showChat() {}
    Q_INVOKABLE void openOverview() {}
    Q_INVOKABLE void openProject(const QString &) {}
    Q_INVOKABLE void deleteProject(const QString &) {}
    Q_INVOKABLE void renameProject(const QString &, const QString &) {}

signals:
    void changed();
};

// A Shammy module dir so Sidebar can see Theme and its sibling types.
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
        "Sidebar 1.0 Sidebar.qml\n"
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
    width: 280
    height: 640
    visible: true
    Sidebar { anchors.fill: parent }
}
)qml";

// Depth-first walk. List delegates are visual children, not always findChildren.
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

QQuickItem *findText(QQuickItem *root, const QString &text)
{
    QList<QQuickItem *> all;
    collectItems(root, &all);
    for (QQuickItem *item : all)
    {
        if (item->property("text").toString() == text)
        {
            return item;
        }
    }
    return nullptr;
}

// The generating row's spinner. A hidden row keeps the first frame and must
// not be counted as the running one.
QQuickItem *visibleSpinner(QQuickItem *root)
{
    const QStringList frames = {
        QStringLiteral("⠋"), QStringLiteral("⠙"), QStringLiteral("⠹"),
        QStringLiteral("⠸"), QStringLiteral("⠼"), QStringLiteral("⠴"),
        QStringLiteral("⠦"), QStringLiteral("⠧"), QStringLiteral("⠇"),
        QStringLiteral("⠏"),
    };
    QList<QQuickItem *> all;
    collectItems(root, &all);
    for (QQuickItem *item : all)
    {
        if (!frames.contains(item->property("text").toString()))
        {
            continue;
        }
        if (item->isVisible() && item->width() > 1)
        {
            return item;
        }
    }
    return nullptr;
}

QPoint scenePoint(QQuickItem *item, qreal x, qreal y)
{
    return item->mapToScene(QPointF(x, y)).toPoint();
}

// How many times `item` hides or shows, and how many spinner frames pass.
struct Watch
{
    int flips = 0;
    int spinChanges = 0;
};

Watch watch(QQuickItem *closeItem, QQuickItem *spinner, int ms)
{
    Watch result;
    QString last = spinner ? spinner->property("text").toString() : QString();
    const QMetaObject::Connection conn = QObject::connect(
        closeItem, &QQuickItem::visibleChanged, closeItem, [&]()
        {
            ++result.flips;
        });
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms)
    {
        QTest::qWait(40);
        if (!spinner)
        {
            continue;
        }
        const QString now = spinner->property("text").toString();
        if (now != last)
        {
            last = now;
            ++result.spinChanges;
        }
    }
    QObject::disconnect(conn);
    return result;
}

} // namespace

class TestSidebar : public QObject
{
    Q_OBJECT

private slots:
    // Pointer on another row's × while a chat generates: the × stays put.
    void closeStaysShownWhileAnotherChatGenerates()
    {
        // The × width binding loops through its own hover area. macOS CI
        // prints other warnings, so only this file fails the test.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("Sidebar\\.qml")));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString moduleDir = dir.filePath(QStringLiteral("Shammy"));
        QVERIFY(prepareModule(moduleDir));

        ConvModel conversations;
        conversations.rows = {
            {QStringLiteral("gen"), QStringLiteral("Generating chat"), false},
            {QStringLiteral("other"), QStringLiteral("Other chat"), false},
        };
        ConvModel favorites;
        ProjectModel projectsModel;

        StubChat chat;
        chat.conversations = &conversations;
        chat.favorites = &favorites;
        chat.setGeneratingConversationId(QStringLiteral("gen"));
        StubProjects projects;
        projects.projects = &projectsModel;
        StubSettings settings;

        QQmlEngine engine;
        engine.addImportPath(dir.path());
        engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
        engine.rootContext()->setContextProperty(QStringLiteral("projects"), &projects);
        engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);

        QQmlComponent component(&engine);
        component.setData(kDocument, QUrl());
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QQuickItem *content = window->contentItem();
        QTRY_VERIFY_WITH_TIMEOUT(findText(content, QStringLiteral("Other chat")) != nullptr, 2000);
        QQuickItem *title = findText(content, QStringLiteral("Other chat"));
        QVERIFY(title);
        QQuickItem *row = title->parentItem();
        QVERIFY(row);
        QTRY_VERIFY(row->width() > 50);
        QTRY_VERIFY(row->height() > 10);

        // The other row's spinner is the dirty source. It has to be running.
        QTRY_VERIFY_WITH_TIMEOUT(visibleSpinner(content) != nullptr, 1000);

        // Title side of the row, not the ×. The control should appear and stay.
        // Hidden, the × takes no width, so the title reaches further right.
        QPointer<QQuickItem> rowWatch(row);
        QQuickItem *closeBefore = findText(row, QStringLiteral("×"));
        QVERIFY(closeBefore);
        QVERIFY(closeBefore->width() < 1.0);
        const qreal titleRightIdle = title->mapToItem(content, QPointF(title->width(), 0)).x();
        const QPoint titlePt = scenePoint(row, 24, row->height() / 2.0);
        QTest::mouseMove(window, titlePt);
        QTest::qWait(150);
        QQuickItem *closeItem = findText(row, QStringLiteral("×"));
        QVERIFY2(closeItem, "close control did not appear on row hover");
        QVERIFY(closeItem->isVisible());
        QVERIFY(closeItem->width() > 1);
        const qreal titleRightHot = title->mapToItem(content, QPointF(title->width(), 0)).x();
        QVERIFY2(titleRightHot < titleRightIdle,
                 "showing the × did not give the title back its trailing space");

        // Rest the pointer on the × itself for 800 ms.
        const QPoint closePt = scenePoint(closeItem, closeItem->width() / 2.0,
                                           closeItem->height() / 2.0);
        QTest::mouseMove(window, closePt);
        // Let the move land. The bug then keeps flipping; a fixed × stays shown.
        QTest::qWait(80);
        QQuickItem *spinner = visibleSpinner(content);
        QVERIFY(spinner);
        const Watch parked = watch(closeItem, spinner, 800);
        QVERIFY2(parked.spinChanges >= 5, "spinner did not advance; the dirty frames were missing");
        QCOMPARE(parked.flips, 0);
        QVERIFY(!rowWatch.isNull());
        QCOMPARE(rowWatch.data(), row);
        QVERIFY(closeItem->isVisible());

        // The × still opens the confirm dialog.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, closePt);
        QTRY_VERIFY(findText(content, QStringLiteral("Delete chat?")) != nullptr);
        QQuickItem *cancel = findText(content, QStringLiteral("Cancel"));
        QVERIFY(cancel);
        const QPoint cancelPt = scenePoint(cancel, cancel->width() / 2.0, cancel->height() / 2.0);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, cancelPt);
        QQuickItem *heading = nullptr;
        const auto dialogGone = [&]() {
            heading = findText(content, QStringLiteral("Delete chat?"));
            return heading == nullptr || !heading->isVisible();
        };
        QTRY_VERIFY_WITH_TIMEOUT(dialogGone(), 1000);

        // Leaving the row hides the ×.
        QTest::mouseMove(window, QPoint(8, 8));
        QTest::qWait(150);
        QVERIFY(!closeItem->isVisible());
    }
};

QTEST_MAIN(TestSidebar)
#include "test_sidebar.moc"
