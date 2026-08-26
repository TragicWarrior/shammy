#include "import/ClaudeCookies.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPasswordDigestor>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#if defined(Q_OS_LINUX) && defined(SHAMMY_DBUS)
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QEventLoop>
#include <QMap>
#include <QTimer>
#endif
#ifdef Q_OS_MACOS
#include <QProcess>
#endif

#ifdef SHAMMY_OPENSSL
#include <openssl/evp.h>
#endif

#if defined(Q_OS_LINUX) && defined(SHAMMY_DBUS)
class SecretPromptWaiter : public QObject
{
    Q_OBJECT
public:
    QEventLoop loop;
public slots:
    void completed(bool, const QDBusVariant &) { loop.quit(); }
};
#endif

static QString cookieDbPath()
{
#ifdef Q_OS_MACOS
    return QDir::homePath() + QStringLiteral("/Library/Application Support/Claude/Cookies");
#elif defined(Q_OS_WIN)
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/Claude/Cookies");
#else
    return QDir::homePath() + QStringLiteral("/.config/Claude/Cookies");
#endif
}

static QByteArray chromiumOsCryptKey(QString *error)
{
#if defined(Q_OS_LINUX) && defined(SHAMMY_DBUS)
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    qDBusRegisterMetaType<QMap<QString, QString>>();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
    {
        if (error)
            *error = QStringLiteral("No D-Bus session; cannot read the Claude keyring.");
        return {};
    }

    auto dbusError = [](const QDBusMessage &reply) -> QString
    {
        if (reply.type() != QDBusMessage::ErrorMessage)
            return {};
        QString m = reply.errorMessage();
        if (m.isEmpty())
            m = reply.errorName();
        return m;
    };

    auto readPaths = [](const QVariant &v) -> QList<QDBusObjectPath>
    {
        QList<QDBusObjectPath> out;
        if (v.canConvert<QDBusArgument>())
            v.value<QDBusArgument>() >> out;
        return out;
    };

    auto runPrompt = [&](const QDBusObjectPath &prompt)
    {
        if (prompt.path().isEmpty() || prompt.path() == QLatin1String("/"))
            return;
        SecretPromptWaiter waiter;
        bus.connect(QStringLiteral("org.freedesktop.secrets"),
                    prompt.path(),
                    QStringLiteral("org.freedesktop.Secret.Prompt"),
                    QStringLiteral("Completed"),
                    &waiter,
                    SLOT(completed()));
        QDBusMessage pr = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.secrets"),
            prompt.path(),
            QStringLiteral("org.freedesktop.Secret.Prompt"),
            QStringLiteral("Prompt"));
        pr << QString();
        bus.call(pr, QDBus::Block, 4000);
        QTimer::singleShot(120000, &waiter.loop, &QEventLoop::quit);
        waiter.loop.exec();
    };

    QDBusMessage open = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.secrets"),
        QStringLiteral("/org/freedesktop/secrets"),
        QStringLiteral("org.freedesktop.Secret.Service"),
        QStringLiteral("OpenSession"));
    open << QStringLiteral("plain") << QVariant::fromValue(QDBusVariant(QString()));
    QDBusMessage openReply = bus.call(open, QDBus::Block, 8000);
    if (openReply.type() == QDBusMessage::ErrorMessage || openReply.arguments().size() < 2)
    {
        if (error)
            *error = QStringLiteral("Could not open the system keyring: %1")
                         .arg(dbusError(openReply).isEmpty() ? QStringLiteral("no reply") : dbusError(openReply));
        return {};
    }
    const QDBusObjectPath session = openReply.arguments().at(1).value<QDBusObjectPath>();

    QMap<QString, QString> attrs;
    attrs.insert(QStringLiteral("application"), QStringLiteral("Claude"));
    QDBusMessage search = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.secrets"),
        QStringLiteral("/org/freedesktop/secrets"),
        QStringLiteral("org.freedesktop.Secret.Service"),
        QStringLiteral("SearchItems"));
    search << QVariant::fromValue(attrs);
    QDBusMessage searchReply = bus.call(search, QDBus::Block, 8000);
    if (searchReply.type() == QDBusMessage::ErrorMessage || searchReply.arguments().isEmpty())
    {
        if (error)
            *error = QStringLiteral("Claude Desktop keyring entry not found (%1).")
                         .arg(dbusError(searchReply).isEmpty() ? QStringLiteral("empty result") : dbusError(searchReply));
        return {};
    }
    QList<QDBusObjectPath> items = readPaths(searchReply.arguments().at(0));
    QList<QDBusObjectPath> locked;
    if (searchReply.arguments().size() > 1)
        locked = readPaths(searchReply.arguments().at(1));
    if (items.isEmpty())
        items = locked;
    if (items.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Claude Desktop keyring entry not found. Open Claude Desktop once so it can create it.");
        return {};
    }

    QDBusMessage unlock = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.secrets"),
        QStringLiteral("/org/freedesktop/secrets"),
        QStringLiteral("org.freedesktop.Secret.Service"),
        QStringLiteral("Unlock"));
    unlock << QVariant::fromValue(items);
    QDBusMessage unlockReply = bus.call(unlock, QDBus::Block, 8000);
    if (unlockReply.type() != QDBusMessage::ErrorMessage && unlockReply.arguments().size() >= 2)
        runPrompt(unlockReply.arguments().at(1).value<QDBusObjectPath>());

    QDBusMessage get = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.secrets"),
        items.first().path(),
        QStringLiteral("org.freedesktop.Secret.Item"),
        QStringLiteral("GetSecret"));
    get << QVariant::fromValue(session);
    QDBusMessage getReply = bus.call(get, QDBus::Block, 15000);
    if (getReply.type() == QDBusMessage::ErrorMessage || getReply.arguments().isEmpty())
    {
        if (error)
            *error = QStringLiteral("Could not read the Claude keyring secret: %1")
                         .arg(dbusError(getReply).isEmpty() ? QStringLiteral("empty reply") : dbusError(getReply));
        return {};
    }
    const QDBusArgument secretArg = getReply.arguments().at(0).value<QDBusArgument>();
    secretArg.beginStructure();
    QDBusObjectPath sess;
    QByteArray params;
    QByteArray value;
    QString contentType;
    secretArg >> sess >> params >> value >> contentType;
    secretArg.endStructure();
    if (value.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Claude keyring secret was empty.");
        return {};
    }
    return value;
#elif defined(Q_OS_MACOS)
    auto lookup = [](const QString &service, const QString &account) -> QByteArray
    {
        QProcess p;
        p.start(QStringLiteral("security"),
                {QStringLiteral("find-generic-password"), QStringLiteral("-w"),
                 QStringLiteral("-s"), service, QStringLiteral("-a"), account});
        p.waitForFinished(4000);
        return p.exitCode() == 0 ? p.readAllStandardOutput().trimmed() : QByteArray();
    };
    QByteArray secret = lookup(QStringLiteral("Chromium Safe Storage"), QStringLiteral("Claude"));
    if (secret.isEmpty())
        secret = lookup(QStringLiteral("Chrome Safe Storage"), QStringLiteral("Claude"));
    if (secret.isEmpty() && error)
        *error = QStringLiteral("Could not read Claude's Keychain entry. Open Claude Desktop once and try again.");
    return secret;
#else
    if (error)
        *error = QStringLiteral("Import from Claude is not supported on this platform yet.");
    return {};
#endif
}

#ifdef SHAMMY_OPENSSL
static QByteArray aes128CbcDecrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &ct)
{
    if (key.size() != 16 || iv.size() != 16 || ct.isEmpty())
        return {};
    QByteArray out(ct.size(), 0);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};
    int len = 0;
    int total = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(iv.constData()))
        != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &len,
                          reinterpret_cast<const unsigned char *>(ct.constData()), ct.size())
        != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    total = len;
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + len, &len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    total += len;
    EVP_CIPHER_CTX_free(ctx);
    out.resize(total);
    return out;
}
#endif

static QString decryptCookieValue(const QByteArray &encrypted, const QByteArray &osCryptKey)
{
    if (encrypted.size() < 19)
        return {};
    const QByteArray prefix = encrypted.left(3);
    if (prefix != "v10" && prefix != "v11")
        return {};
#ifdef SHAMMY_OPENSSL
    const QByteArray key = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha1, osCryptKey, QByteArrayLiteral("saltysalt"), 1, 16);
    const QByteArray iv(16, ' ');
    QByteArray pt = aes128CbcDecrypt(key, iv, encrypted.mid(3));
    if (pt.size() <= 32)
        return {};
    return QString::fromUtf8(pt.mid(32));
#else
    Q_UNUSED(osCryptKey);
    return {};
#endif
}

bool loadClaudeSessionCookies(ClaudeSessionCookies *out, QString *error)
{
    const QString src = cookieDbPath();
    if (!QFileInfo::exists(src))
    {
        if (error)
            *error = QStringLiteral("Claude Desktop data not found at %1").arg(src);
        return false;
    }
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        if (error)
            *error = QStringLiteral("Could not copy Claude cookies.");
        return false;
    }
    const QString dest = tmp.filePath(QStringLiteral("Cookies"));
    QFile::copy(src, dest);
    QFile::copy(src + QStringLiteral("-wal"), dest + QStringLiteral("-wal"));
    QFile::copy(src + QStringLiteral("-shm"), dest + QStringLiteral("-shm"));

    const QString conn = QStringLiteral("claude-cookies-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(dest);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open())
        {
            QSqlDatabase::removeDatabase(conn);
            if (error)
                *error = QStringLiteral("Could not open Claude's cookie database.");
            return false;
        }
        QByteArray osKey = chromiumOsCryptKey(error);
        if (osKey.isEmpty())
        {
            db.close();
            QSqlDatabase::removeDatabase(conn);
            return false;
        }
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT name, value, encrypted_value, host_key FROM cookies")))
        {
            db.close();
            QSqlDatabase::removeDatabase(conn);
            if (error)
                *error = QStringLiteral("Could not read Claude cookies.");
            return false;
        }
        QStringList parts;
        QString org;
        QString session;
        while (q.next())
        {
            const QString host = q.value(3).toString();
            if (!host.contains(QStringLiteral("claude.ai")))
                continue;
            const QString name = q.value(0).toString();
            QString value = q.value(1).toString();
            if (value.isEmpty())
                value = decryptCookieValue(q.value(2).toByteArray(), osKey);
            if (value.isEmpty())
                continue;
            parts.append(name + QLatin1Char('=') + value);
            if (name == QLatin1String("lastActiveOrg"))
                org = value;
            if (name == QLatin1String("sessionKey") || name == QLatin1String("sessionKeyV3"))
                session = value;
        }
        db.close();
        QSqlDatabase::removeDatabase(conn);
        if (session.isEmpty() || org.isEmpty())
        {
            if (error)
                *error = QStringLiteral("No signed-in Claude session found. Open Claude Desktop, sign in, and try again.");
            return false;
        }
        out->orgId = org;
        out->header = parts.join(QStringLiteral("; "));
        return true;
    }
}

#if defined(Q_OS_LINUX) && defined(SHAMMY_DBUS)
#include "ClaudeCookies.moc"
#endif
