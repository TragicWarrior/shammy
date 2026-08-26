#include "mcp/McpConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtGlobal>

static QString currentDefaultPath()
{
#if defined(Q_OS_WIN)
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("Shammy/config.json"));
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + QStringLiteral("/Library/Application Support/Shammy/config.json");
#else
    return QDir::homePath() + QStringLiteral("/.shammy/config.json");
#endif
}

static QString legacyDefaultPath()
{
#if defined(Q_OS_WIN)
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("LlamaChat/config.json"));
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + QStringLiteral("/Library/Application Support/LlamaChat/config.json");
#else
    return QDir::homePath() + QStringLiteral("/.llamachat/config.json");
#endif
}

QString McpConfig::defaultPath()
{
    const QString neu = currentDefaultPath();
    if (QFile::exists(neu))
        return neu;
    const QString old = legacyDefaultPath();
    if (QFile::exists(old))
    {
        QDir().mkpath(QFileInfo(neu).absolutePath());
        QFile::copy(old, neu);
    }
    return neu;
}

QStringList McpConfig::importCandidates()
{
    const QString home = QDir::homePath();
    QStringList out;
    out << home + QStringLiteral("/.config/Claude/claude_desktop_config.json");
    out << home + QStringLiteral("/Library/Application Support/Claude/claude_desktop_config.json");
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
    {
        out << appData + QStringLiteral("/Claude/claude_desktop_config.json");
    }
    out << QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/mcp.json");
    out << home + QStringLiteral("/.config/shammy/mcp.json");
    out << home + QStringLiteral("/.config/llamachat/mcp.json");
    out << home + QStringLiteral("/.llamachat/config.json");
    out.removeDuplicates();
    return out;
}

bool McpConfig::readJsonFile(const QString &path, QJsonObject *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
    {
        return false;
    }
    *out = doc.object();
    return true;
}

QJsonObject McpConfig::mcpServersOnly(const QJsonObject &root)
{
    QJsonObject out;
    const QJsonValue servers = root.value(QStringLiteral("mcpServers"));
    if (servers.isObject())
    {
        out.insert(QStringLiteral("mcpServers"), servers);
    }
    return out;
}

QList<McpServerConfig> McpConfig::parseServers(const QJsonObject &root)
{
    QList<McpServerConfig> out;
    const QJsonObject servers = root.value(QStringLiteral("mcpServers")).toObject();
    for (auto it = servers.begin(); it != servers.end(); ++it)
    {
        const QJsonObject o = it.value().toObject();
        McpServerConfig c;
        c.name = it.key();
        c.command = o.value(QStringLiteral("command")).toString();
        for (const auto &a : o.value(QStringLiteral("args")).toArray())
        {
            c.args.append(a.toString());
        }
        const QJsonObject env = o.value(QStringLiteral("env")).toObject();
        for (auto e = env.begin(); e != env.end(); ++e)
        {
            c.env.insert(e.key(), e.value().toString());
        }
        c.cwd = o.value(QStringLiteral("cwd")).toString();
        c.enabled = !o.value(QStringLiteral("disabled")).toBool(false);
        c.extra = o;
        for (const char *k : {"command", "args", "env", "cwd", "disabled"})
        {
            c.extra.remove(QLatin1String(k));
        }
        out.append(c);
    }
    return out;
}

QJsonObject McpConfig::mergeServers(QJsonObject root, const QList<McpServerConfig> &cfgs)
{
    QJsonObject servers;
    for (const McpServerConfig &c : cfgs)
    {
        QJsonObject o = c.extra;
        o.insert(QStringLiteral("command"), c.command);
        if (!c.args.isEmpty())
        {
            QJsonArray args;
            for (const QString &a : c.args)
            {
                args.append(a);
            }
            o.insert(QStringLiteral("args"), args);
        }
        else
        {
            o.remove(QStringLiteral("args"));
        }
        if (!c.env.isEmpty())
        {
            QJsonObject env;
            for (auto it = c.env.begin(); it != c.env.end(); ++it)
            {
                env.insert(it.key(), it.value());
            }
            o.insert(QStringLiteral("env"), env);
        }
        else
        {
            o.remove(QStringLiteral("env"));
        }
        if (!c.cwd.isEmpty())
        {
            o.insert(QStringLiteral("cwd"), c.cwd);
        }
        else
        {
            o.remove(QStringLiteral("cwd"));
        }
        if (!c.enabled)
        {
            o.insert(QStringLiteral("disabled"), true);
        }
        else
        {
            o.remove(QStringLiteral("disabled"));
        }
        servers.insert(c.name, o);
    }
    root.insert(QStringLiteral("mcpServers"), servers);
    return root;
}
