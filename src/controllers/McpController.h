#pragma once

#include "mcp/McpHost.h"
#include "models/SimpleListModels.h"
#include "persist/Store.h"

#include <QObject>
#include <QVariantList>

class McpController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(McpServerListModel *servers READ servers CONSTANT)
    Q_PROPERTY(int toolCount READ toolCount NOTIFY toolsChanged)
    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY serversChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logUpdated)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
public:
    McpController(McpHost *host, Store *store, QObject *parent = nullptr);

    McpServerListModel *servers() { return &m_servers; }
    int toolCount() const;
    int connectedCount() const { return m_host->connectedCount(); }
    QString logText() const { return m_host->combinedLog(); }
    QString configPath() const { return m_host->configPath(); }

    Q_INVOKABLE void addServer(const QString &name, const QString &command, const QString &argsJoined);
    Q_INVOKABLE void removeServer(const QString &name);
    Q_INVOKABLE void setEnabled(const QString &name, bool enabled);
    Q_INVOKABLE void restart(const QString &name);
    Q_INVOKABLE void reload();
    Q_INVOKABLE QVariantList serverSnapshot() const;
    Q_INVOKABLE void applyServerSnapshot(const QVariantList &rows);

    McpHost *host() const { return m_host; }
    Store *store() const { return m_store; }

signals:
    void serversChanged();
    void toolsChanged();
    void logUpdated();

private:
    void refresh();
    McpHost *m_host = nullptr;
    Store *m_store = nullptr;
    McpServerListModel m_servers;
};
