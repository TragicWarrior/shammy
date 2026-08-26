#include "controllers/ChatController.h"
#include "controllers/McpController.h"
#include "controllers/ProjectController.h"
#include "controllers/SettingsController.h"
#include "import/ClaudeImporter.h"
#include "mcp/McpConfig.h"
#include "mcp/McpHost.h"
#include "openai/OpenAiClient.h"
#include "persist/Store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QWindow>

#ifdef SHAMMY_WEBENGINE
#include <QtWebEngineQuick>
#endif

static bool copyFileReplace(const QString &from, const QString &to)
{
    if (!QFile::exists(from))
        return false;
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile::remove(to);
    return QFile::copy(from, to);
}

static void copyDir(const QString &from, const QString &to)
{
    if (!QDir(from).exists())
        return;
    QDir().mkpath(to);
    const QFileInfoList entries = QDir(from).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries)
    {
        const QString dest = to + QLatin1Char('/') + fi.fileName();
        if (fi.isDir())
            copyDir(fi.absoluteFilePath(), dest);
        else if (!QFile::exists(dest))
            QFile::copy(fi.absoluteFilePath(), dest);
    }
}

static bool sqliteHasUserData(const QString &path)
{
    if (!QFile::exists(path) || QFileInfo(path).size() < 1024)
        return false;
    const QString conn = QStringLiteral("shammy-migrate-check");
    if (QSqlDatabase::contains(conn))
        QSqlDatabase::removeDatabase(conn);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(path);
    if (!db.open())
    {
        QSqlDatabase::removeDatabase(conn);
        return false;
    }
    int n = 0;
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM conversations")) && q.next())
        n += q.value(0).toInt();
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM projects")) && q.next())
        n += q.value(0).toInt();
    db.close();
    QSqlDatabase::removeDatabase(conn);
    return n > 0;
}

static QString findLegacyLlamaChatDb()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QStringList candidates = {
        root + QStringLiteral("/llamachat/llamachat/llamachat.db"),
        root + QStringLiteral("/llamachat/llamachat.db"),
        QDir::homePath() + QStringLiteral("/.local/share/llamachat/llamachat/llamachat.db"),
    };
    for (const QString &p : candidates)
    {
        if (sqliteHasUserData(p))
            return p;
    }
    return {};
}

static void migrateFromLlamaChat()
{
    const QString newData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString newDb = newData + QStringLiteral("/shammy.db");
    if (sqliteHasUserData(newDb))
        return;
    const QString oldDb = findLegacyLlamaChatDb();
    if (oldDb.isEmpty())
        return;
    copyFileReplace(oldDb, newDb);
    const QString oldData = QFileInfo(oldDb).absolutePath();
    copyDir(oldData + QStringLiteral("/projects"), newData + QStringLiteral("/projects"));
    const QString genericConfig = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    copyFileReplace(genericConfig + QStringLiteral("/llamachat/llamachat.conf"),
                    genericConfig + QStringLiteral("/shammy/shammy.conf"));
}

static QIcon shammyIcon()
{
    QIcon icon;
    for (int s : {16, 22, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/shammy-%1.png").arg(s), QSize(s, s));
    return icon;
}

int main(int argc, char *argv[])
{
#ifdef SHAMMY_WEBENGINE
    QtWebEngineQuick::initialize();
#endif
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("shammy"));
    app.setOrganizationDomain(QStringLiteral("shammy.local"));
    app.setApplicationName(QStringLiteral("shammy"));
    app.setApplicationDisplayName(QStringLiteral("Shammy"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setDesktopFileName(QStringLiteral("shammy"));
    app.setWindowIcon(shammyIcon());
    migrateFromLlamaChat();

    Store store;
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    if (!store.open(dataDir + QStringLiteral("/shammy.db")))
    {
        qWarning("failed to open database: %s", qPrintable(store.lastError()));
        return 1;
    }

    OpenAiClient openai;
    McpHost mcpHost;
    mcpHost.setConfigPath(McpConfig::defaultPath());
    mcpHost.load();
    mcpHost.startEnabled();

    SettingsController settings(&store, &openai);
    ProjectController projects(&store);
    ClaudeImporter claudeImport(&projects, &store, &settings);
    McpController mcp(&mcpHost, &store);
    ChatController chat(&store, &openai, &mcp, &projects, &settings);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("projects"), &projects);
    engine.rootContext()->setContextProperty(QStringLiteral("claudeImport"), &claudeImport);
    engine.rootContext()->setContextProperty(QStringLiteral("mcp"), &mcp);
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Shammy/Main.qml")));
    if (engine.rootObjects().isEmpty())
    {
        return 1;
    }
    if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst()))
    {
        const QScreen *scr = window->screen() ? window->screen() : QGuiApplication::primaryScreen();
        if (scr)
        {
            QRect frame = window->frameGeometry();
            if (frame.isEmpty())
            {
                frame.setSize(window->size());
            }
            frame.moveCenter(scr->availableGeometry().center());
            window->setFramePosition(frame.topLeft());
        }
        window->setIcon(QGuiApplication::windowIcon());
        window->show();
    }
    return app.exec();
}
