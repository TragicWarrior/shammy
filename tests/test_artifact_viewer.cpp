#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

// ArtifactViewer reads Loader.errorString at startup. Loader has no such
// property, so the read is undefined and indexOf throws. macOS CI prints
// unrelated warnings; only this file fails the test.

namespace
{

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
    const QString from = components + QStringLiteral("/ArtifactViewer.qml");
    const QString to = dir + QStringLiteral("/ArtifactViewer.qml");
    if (!QFile::link(from, to))
    {
        return false;
    }
    QFile qmldir(dir + QStringLiteral("/qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    const QByteArray text =
        "module Shammy\n"
        "singleton Theme 1.0 Theme.qml\n"
        "ArtifactViewer 1.0 ArtifactViewer.qml\n";
    return qmldir.write(text) == text.size();
}

const char *kDocument = R"qml(
import QtQuick
import QtQuick.Window
import Shammy 1.0
Window {
    width: 640
    height: 480
    visible: true
    ArtifactViewer { anchors.fill: parent }
}
)qml";

// What ArtifactViewer reads off the context properties.
class StubSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasWebEngine MEMBER hasWebEngine NOTIFY changed)

public:
    bool hasWebEngine = false;

signals:
    void changed();
};

class StubChat : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool currentArtifactCanPreview MEMBER currentArtifactCanPreview NOTIFY changed)
    Q_PROPERTY(QString currentArtifactContent MEMBER currentArtifactContent NOTIFY changed)

public:
    bool currentArtifactCanPreview = false;
    QString currentArtifactContent;

signals:
    void changed();
};

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

} // namespace

class TestArtifactViewer : public QObject
{
    Q_OBJECT

private slots:
    void startupDoesNotReadIndexOfOnUndefined()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("ArtifactViewer\\.qml")));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(prepareModule(dir.filePath(QStringLiteral("Shammy"))));

        StubSettings settings;
        StubChat chat;
        QQmlEngine engine;
        engine.addImportPath(dir.path());
        engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
        engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);

        QQmlComponent component(&engine);
        component.setData(kDocument, QUrl());
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // The mode switch is up. The error column stays hidden: nothing is
        // previewing, and the loader was never in Error.
        QTRY_VERIFY(findText(window->contentItem(), QStringLiteral("Preview")) != nullptr);
        QQuickItem *failed = findText(window->contentItem(),
                                      QStringLiteral("HTML preview could not start."));
        QVERIFY(failed);
        QVERIFY(!failed->isVisible());
    }
};

QTEST_MAIN(TestArtifactViewer)
#include "test_artifact_viewer.moc"
