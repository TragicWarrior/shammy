#include "controllers/McpController.h"

#include <QHash>
#include <QVariantMap>

McpController::McpController(McpHost *host, Store *store, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_store(store)
{
    connect(m_host, &McpHost::serversChanged, this, [this]()
    {
        refresh();
        emit serversChanged();
    });
    connect(m_host, &McpHost::toolsChanged, this, &McpController::toolsChanged);
    connect(m_host, &McpHost::logUpdated, this, &McpController::logUpdated);
    refresh();
}

int McpController::toolCount() const
{
    return m_host->allTools().size();
}

void McpController::refresh()
{
    QList<McpServerListModel::Row> rows;
    for (const McpServerConfig &c : m_host->configs())
    {
        McpServerListModel::Row r;
        r.name = c.name;
        r.command = c.command + QLatin1Char(' ') + c.args.join(QLatin1Char(' '));
        r.enabled = c.enabled;
        r.state = QStringLiteral("stopped");
        if (auto *cl = m_host->client(c.name))
        {
            switch (cl->state())
            {
            case McpClient::State::Starting:
                r.state = QStringLiteral("starting");
                break;
            case McpClient::State::Connected:
                r.state = QStringLiteral("connected");
                r.toolCount = cl->tools().size();
                break;
            case McpClient::State::Error:
                r.state = QStringLiteral("error");
                r.error = cl->errorString();
                break;
            default:
                r.state = QStringLiteral("stopped");
                break;
            }
        }
        rows.append(r);
    }
    m_servers.setItems(rows);
}

void McpController::addServer(const QString &name, const QString &command, const QString &argsJoined)
{
    McpServerConfig c;
    c.name = name.trimmed().isEmpty() ? QStringLiteral("server") : name.trimmed();
    c.command = command.trimmed();
    const QStringList parts = argsJoined.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    c.args = parts;
    c.enabled = true;
    m_host->upsertServer(c);
}

void McpController::removeServer(const QString &name)
{
    m_host->removeServer(name);
}

void McpController::setEnabled(const QString &name, bool enabled)
{
    m_host->setEnabled(name, enabled);
}

void McpController::restart(const QString &name)
{
    m_host->restart(name);
}

void McpController::reload()
{
    m_host->load();
    m_host->startEnabled();
}

QVariantList McpController::serverSnapshot() const
{
    QVariantList out;
    for (const McpServerConfig &c : m_host->configs())
    {
        QVariantMap row;
        row.insert(QStringLiteral("name"), c.name);
        row.insert(QStringLiteral("command"), c.command);
        row.insert(QStringLiteral("args"), c.args.join(QLatin1Char(' ')));
        row.insert(QStringLiteral("enabled"), c.enabled);
        QString state = QStringLiteral("stopped");
        QString error;
        int toolCount = 0;
        if (auto *cl = m_host->client(c.name))
        {
            switch (cl->state())
            {
            case McpClient::State::Starting:
                state = QStringLiteral("starting");
                break;
            case McpClient::State::Connected:
                state = QStringLiteral("connected");
                toolCount = cl->tools().size();
                break;
            case McpClient::State::Error:
                state = QStringLiteral("error");
                error = cl->errorString();
                break;
            default:
                break;
            }
        }
        row.insert(QStringLiteral("state"), state);
        row.insert(QStringLiteral("toolCount"), toolCount);
        row.insert(QStringLiteral("error"), error);
        out.append(row);
    }
    return out;
}

void McpController::applyServerSnapshot(const QVariantList &rows)
{
    QHash<QString, McpServerConfig> prev;
    for (const McpServerConfig &c : m_host->configs())
    {
        prev.insert(c.name, c);
    }
    QList<McpServerConfig> next;
    for (const QVariant &v : rows)
    {
        const QVariantMap m = v.toMap();
        McpServerConfig c;
        c.name = m.value(QStringLiteral("name")).toString();
        c.command = m.value(QStringLiteral("command")).toString();
        c.args = m.value(QStringLiteral("args")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        c.enabled = m.value(QStringLiteral("enabled")).toBool();
        if (prev.contains(c.name))
        {
            const McpServerConfig &old = prev.value(c.name);
            c.env = old.env;
            c.cwd = old.cwd;
            c.extra = old.extra;
        }
        if (!c.name.isEmpty() && !c.command.isEmpty())
        {
            next.append(c);
        }
    }
    m_host->replaceConfigs(next);
}
