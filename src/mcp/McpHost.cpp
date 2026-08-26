#include "mcp/McpHost.h"
#include "mcp/McpConfig.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

McpHost::McpHost(QObject *parent)
    : QObject(parent)
{
}

void McpHost::setConfigPath(const QString &path)
{
    m_path = path;
}

void McpHost::importIfMissing()
{
    if (QFile::exists(m_path))
    {
        return;
    }
    for (const QString &cand : McpConfig::importCandidates())
    {
        QJsonObject root;
        if (!McpConfig::readJsonFile(cand, &root))
        {
            continue;
        }
        const QJsonObject slim = McpConfig::mcpServersOnly(root);
        if (slim.value(QStringLiteral("mcpServers")).toObject().isEmpty())
        {
            continue;
        }
        m_document = slim;
        m_configs = McpConfig::parseServers(slim);
        save();
        return;
    }
}

bool McpHost::load()
{
    m_configs.clear();
    m_document = QJsonObject{};
    QFile f(m_path);
    if (!f.exists())
    {
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        importIfMissing();
        if (m_configs.isEmpty())
        {
            save();
        }
        rebuildClients();
        return true;
    }
    QJsonObject root;
    if (!McpConfig::readJsonFile(m_path, &root))
    {
        return false;
    }
    m_document = root;
    m_configs = McpConfig::parseServers(root);
    rebuildClients();
    return true;
}

bool McpHost::save() const
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    const QJsonObject root = McpConfig::mergeServers(m_document, m_configs);
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void McpHost::rebuildClients()
{
    qDeleteAll(m_clients);
    m_clients.clear();
    for (const McpServerConfig &c : m_configs)
    {
        auto *client = new McpClient(c, this);
        connect(client, &McpClient::stateChanged, this, &McpHost::serversChanged);
        connect(client, &McpClient::toolsChanged, this, [this]()
        {
            disambiguateNames();
            emit toolsChanged();
        });
        connect(client, &McpClient::logUpdated, this, &McpHost::logUpdated);
        m_clients.append(client);
    }
    emit serversChanged();
    emit toolsChanged();
}

void McpHost::upsertServer(const McpServerConfig &cfg)
{
    bool found = false;
    for (auto &c : m_configs)
    {
        if (c.name == cfg.name)
        {
            c = cfg;
            found = true;
            break;
        }
    }
    if (!found)
        m_configs.append(cfg);
    save();
    rebuildClients();
    startEnabled();
}

void McpHost::removeServer(const QString &name)
{
    m_configs.erase(std::remove_if(m_configs.begin(), m_configs.end(),
                                   [&](const McpServerConfig &c) { return c.name == name; }),
                    m_configs.end());
    save();
    rebuildClients();
    startEnabled();
}

void McpHost::replaceConfigs(const QList<McpServerConfig> &cfgs)
{
    m_configs = cfgs;
    save();
    rebuildClients();
    startEnabled();
}

void McpHost::setEnabled(const QString &name, bool enabled)
{
    for (auto &c : m_configs)
    {
        if (c.name == name)
            c.enabled = enabled;
    }
    save();
    if (auto *c = client(name))
    {
        if (enabled)
            c->start();
        else
            c->stop();
    }
    emit serversChanged();
}

void McpHost::startEnabled()
{
    for (int i = 0; i < m_clients.size(); ++i)
    {
        if (m_configs[i].enabled)
            m_clients[i]->start();
        else
            m_clients[i]->stop();
    }
}

void McpHost::stopAll()
{
    for (auto *c : m_clients)
        c->stop();
}

void McpHost::restart(const QString &name)
{
    if (auto *c = client(name))
    {
        c->stop();
        c->start();
    }
}

McpClient *McpHost::client(const QString &name) const
{
    for (auto *c : m_clients)
    {
        if (c->config().name == name)
            return c;
    }
    return nullptr;
}

void McpHost::disambiguateNames()
{
    QHash<QString, int> counts;
    for (auto *c : m_clients)
    {
        if (c->state() != McpClient::State::Connected)
            continue;
        for (const McpTool &t : c->tools())
            counts[t.name] += 1;
    }
    for (auto *c : m_clients)
    {
        // tools() returns a copy; we only use exposed names via allTools()
        Q_UNUSED(c);
    }
}

QList<McpTool> McpHost::allTools() const
{
    QHash<QString, int> counts;
    QList<McpTool> raw;
    for (auto *c : m_clients)
    {
        if (c->state() != McpClient::State::Connected)
            continue;
        for (McpTool t : c->tools())
        {
            counts[t.name] += 1;
            raw.append(t);
        }
    }
    QList<McpTool> out;
    QSet<QString> used;
    for (McpTool t : raw)
    {
        if (counts.value(t.name) > 1)
            t.exposedName = t.server + QStringLiteral("__") + t.name;
        else
            t.exposedName = t.name;
        while (used.contains(t.exposedName))
            t.exposedName += QStringLiteral("_");
        used.insert(t.exposedName);
        out.append(t);
    }
    return out;
}

QJsonArray McpHost::openaiTools() const
{
    QJsonArray arr;
    for (const McpTool &t : allTools())
    {
        QJsonObject fn;
        fn.insert(QStringLiteral("name"), t.exposedName);
        fn.insert(QStringLiteral("description"),
                  t.description.isEmpty()
                      ? QStringLiteral("MCP tool %1 from %2").arg(t.name, t.server)
                      : t.description);
        QJsonObject schema = t.inputSchema;
        if (schema.isEmpty())
        {
            schema.insert(QStringLiteral("type"), QStringLiteral("object"));
            schema.insert(QStringLiteral("properties"), QJsonObject{});
        }
        fn.insert(QStringLiteral("parameters"), schema);
        arr.append(QJsonObject
        {
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), fn},
        });
    }
    return arr;
}

McpTool McpHost::findTool(const QString &exposedName) const
{
    for (const McpTool &t : allTools())
    {
        if (t.exposedName == exposedName)
            return t;
    }
    return {};
}

void McpHost::callTool(const QString &exposedName, const QJsonObject &args,
                       const std::function<void(QJsonValue, QString)> &cb)
{
    const McpTool t = findTool(exposedName);
    if (t.name.isEmpty())
    {
        cb({}, QStringLiteral("unknown tool: %1").arg(exposedName));
        return;
    }
    auto *c = client(t.server);
    if (!c)
    {
        cb({}, QStringLiteral("server not found"));
        return;
    }
    c->callTool(t.name, args, cb);
}

QString McpHost::combinedLog() const
{
    QString s;
    for (auto *c : m_clients)
    {
        if (!c->logText().isEmpty())
        {
            s += QStringLiteral("== %1 ==\n").arg(c->config().name);
            s += c->logText();
            s += QLatin1Char('\n');
        }
    }
    return s;
}

int McpHost::connectedCount() const
{
    int n = 0;
    for (auto *c : m_clients)
    {
        if (c->state() == McpClient::State::Connected)
            ++n;
    }
    return n;
}
