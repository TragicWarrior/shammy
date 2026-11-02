#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <utility>

// Behaviour tests for ImageViewer.qml. Every check drives the window the way a
// person would: real key presses and mouse moves, never by calling its
// functions or writing its properties. QTEST_MAIN (not the GUI-less main) so the
// offscreen platform can create the window.

// The offscreen platform cannot raise windows; that warning is expected.
static const char *kOffscreenRaise = "does not support raise()";

// Warnings seen while a test runs.
static QStringList s_warnings;

// Collects warnings into s_warnings; everything else is dropped.
static void collectWarnings(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type == QtWarningMsg)
    {
        s_warnings << msg;
    }
}

// Stub for the "chat" context property: records copy requests.
class StubChat : public QObject
{
    Q_OBJECT

public:
    // Called by the viewer's Copy control and Ctrl+C.
    Q_INVOKABLE bool copyImageToClipboard(const QString &p)
    {
        lastPath = p;
        ++callCount;
        return true;
    }

    QString lastPath;
    int callCount = 0;
};

// One open viewer window on a 400x300 test image, with warning capture.
struct Viewer
{
    QTemporaryDir dir;
    QString pngPath;
    StubChat chat;
    QQmlEngine engine;
    std::unique_ptr<QObject> object;
    QQuickWindow *window = nullptr;
    QtMessageHandler oldHandler = nullptr;

    // Loads ImageViewer.qml, sets the image and opens the window.
    bool open()
    {
        s_warnings.clear();
        oldHandler = qInstallMessageHandler(collectWarnings);

        QImage img(400, 300, QImage::Format_RGB32);
        img.fill(qRgb(200, 60, 60));
        pngPath = dir.path() + QStringLiteral("/test.png");
        if (!img.save(pngPath, "PNG"))
        {
            return false;
        }

        engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(
                                             QML_SOURCE_DIR "/qml/components/ImageViewer.qml")));
        object.reset(component.create());
        if (!object)
        {
            s_warnings << component.errorString();
            return false;
        }
        window = qobject_cast<QQuickWindow *>(object.get());
        if (!window)
        {
            return false;
        }
        object->setProperty("path", pngPath);
        object->setProperty("source", QUrl::fromLocalFile(pngPath));
        object->setProperty("imageWidth", 400);
        object->setProperty("imageHeight", 300);
        QMetaObject::invokeMethod(object.get(), "open");
        return QTest::qWaitForWindowExposed(window);
    }

    // Warnings other than the expected offscreen one.
    QStringList unexpectedWarnings() const
    {
        QStringList out;
        for (const QString &w : std::as_const(s_warnings))
        {
            if (!w.contains(QLatin1String(kOffscreenRaise)))
            {
                out << w;
            }
        }
        return out;
    }

    // Reads a property of the viewer window.
    QVariant prop(const char *name) const
    {
        return object->property(name);
    }

    // The control bar's current opacity.
    double barOpacity() const
    {
        QObject *bar = object->findChild<QObject *>(QStringLiteral("controlBar"));
        return bar ? bar->property("opacity").toDouble() : -1.0;
    }

    // Closes the window, then restores the previous message handler.
    ~Viewer()
    {
        object.reset();
        if (oldHandler)
        {
            qInstallMessageHandler(oldHandler);
        }
    }
};

class TestImageViewer : public QObject
{
    Q_OBJECT

private slots:
    // Opens in fit mode, shows a percentage, and warns about nothing unexpected.
    void opensFittedWithoutWarnings()
    {
        Viewer v;
        QVERIFY2(v.open(), qPrintable(s_warnings.join('\n')));
        QVERIFY(v.prop("fitMode").toBool());
        QCOMPARE(v.prop("shownZoom").toDouble(), v.prop("fitZoom").toDouble());
        // The image is laid out at a real size. On X11 the view is sized only after
        // the window manager answers, so a fit done at open() alone left it at 0x0.
        QObject *flick = v.object->findChild<QObject *>(QStringLiteral("flick"));
        QVERIFY(flick);
        QTRY_VERIFY(flick->property("contentWidth").toDouble() > 0);
        QTRY_VERIFY(flick->property("contentHeight").toDouble() > 0);
        QObject *label = v.object->findChild<QObject *>(QStringLiteral("zoomLabel"));
        QVERIFY(label);
        QVERIFY(label->property("text").toString().endsWith(QLatin1Char('%')));
        QVERIFY2(v.unexpectedWarnings().isEmpty(), qPrintable(v.unexpectedWarnings().join('\n')));
    }

    // The bar is hidden, appears on a mouse move, and fades out once the mouse rests.
    void barAppearsOnMoveAndFades()
    {
        Viewer v;
        QVERIFY2(v.open(), qPrintable(s_warnings.join('\n')));
        QCOMPARE(v.barOpacity(), 0.0);
        QTest::mouseMove(v.window, QPoint(200, 200));
        QTRY_COMPARE_WITH_TIMEOUT(v.barOpacity(), 1.0, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(v.barOpacity(), 0.0, 4000);
        QVERIFY2(v.unexpectedWarnings().isEmpty(), qPrintable(v.unexpectedWarnings().join('\n')));
    }

    // "+" and "=" zoom in, "-" zooms out, "0" returns to fit.
    void plusMinusAndZeroZoom()
    {
        Viewer v;
        QVERIFY2(v.open(), qPrintable(s_warnings.join('\n')));
        const double fit = v.prop("shownZoom").toDouble();

        QTest::keyClick(v.window, '+');
        QTRY_VERIFY(v.prop("shownZoom").toDouble() > fit);
        QVERIFY(!v.prop("fitMode").toBool());
        const double in = v.prop("shownZoom").toDouble();

        QTest::keyClick(v.window, '=');
        QTRY_VERIFY(v.prop("shownZoom").toDouble() > in);
        const double in2 = v.prop("shownZoom").toDouble();

        QTest::keyClick(v.window, '-');
        QTRY_VERIFY(v.prop("shownZoom").toDouble() < in2);

        QTest::keyClick(v.window, '0');
        QTRY_VERIFY(v.prop("fitMode").toBool());
        QCOMPARE(v.prop("shownZoom").toDouble(), v.prop("fitZoom").toDouble());
        QVERIFY2(v.unexpectedWarnings().isEmpty(), qPrintable(v.unexpectedWarnings().join('\n')));
    }

    // A zoom the user chose survives resizing the window.
    void zoomSurvivesResize()
    {
        Viewer v;
        QVERIFY2(v.open(), qPrintable(s_warnings.join('\n')));
        QTest::keyClick(v.window, '+');
        QTRY_VERIFY(!v.prop("fitMode").toBool());
        const double zoom = v.prop("shownZoom").toDouble();

        v.window->setWidth(v.window->width() + 100);
        QTest::qWait(200);
        QVERIFY(!v.prop("fitMode").toBool());
        QCOMPARE(v.prop("shownZoom").toDouble(), zoom);
    }

    // In fit mode, resizing the window re-fits the image.
    void fitFollowsResize()
    {
        Viewer v;
        QVERIFY2(v.open(), qPrintable(s_warnings.join('\n')));
        const double before = v.prop("shownZoom").toDouble();
        v.window->resize(v.window->width() + 200, v.window->height() + 150);
        QTRY_VERIFY(v.prop("shownZoom").toDouble() > before);
        QVERIFY(v.prop("fitMode").toBool());
    }

    // Zooming keeps the image point at the centre of the view where it was.
    void zoomKeepsTheCentre()
    {
        Viewer v;
        QVERIFY2(v.open(), qPrintable(s_warnings.join('\n')));
        QObject *flick = v.object->findChild<QObject *>(QStringLiteral("flick"));
        QVERIFY(flick);
        // The centre of the view as a fraction of the image.
        auto centre = [flick](const char *pos, const char *size, const char *content)
        {
            return (flick->property(pos).toDouble() + flick->property(size).toDouble() / 2)
                / flick->property(content).toDouble();
        };
        for (int i = 0; i < 3; ++i)
        {
            QTest::keyClick(v.window, '+');
        }
        QTRY_VERIFY(!v.prop("fitMode").toBool());
        QVERIFY(v.prop("shownZoom").toDouble() > 1.0); // bigger than the view now
        QVERIFY2(qAbs(centre("contentX", "width", "contentWidth") - 0.5) < 0.02,
                 qPrintable(QString::number(centre("contentX", "width", "contentWidth"))));
        QVERIFY2(qAbs(centre("contentY", "height", "contentHeight") - 0.5) < 0.02,
                 qPrintable(QString::number(centre("contentY", "height", "contentHeight"))));
    }

    // "f" enters full screen, Esc leaves it, and Esc again closes the window.
    void fullScreenAndEscape()
    {
        Viewer v;
        QVERIFY2(v.open(), qPrintable(s_warnings.join('\n')));
        QTest::keyClick(v.window, Qt::Key_F);
        QTRY_COMPARE(v.window->visibility(), QWindow::FullScreen);
        QTest::keyClick(v.window, Qt::Key_Escape);
        QTRY_COMPARE(v.window->visibility(), QWindow::Windowed);
        QTest::keyClick(v.window, Qt::Key_Escape);
        QTRY_VERIFY(!v.window->isVisible());
    }

    // Ctrl+C copies the image once, by its file path.
    void ctrlCCopies()
    {
        Viewer v;
        QVERIFY2(v.open(), qPrintable(s_warnings.join('\n')));
        QTest::keyClick(v.window, Qt::Key_C, Qt::ControlModifier);
        QTRY_COMPARE(v.chat.callCount, 1);
        QCOMPARE(v.chat.lastPath, v.pngPath);
        QVERIFY2(v.unexpectedWarnings().isEmpty(), qPrintable(v.unexpectedWarnings().join('\n')));
    }
};

QTEST_MAIN(TestImageViewer)
#include "test_image_viewer.moc"
