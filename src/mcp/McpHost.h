#pragma once

#include "mcp/McpClient.h"
#include "openai/ChatTypes.h"

#include <QObject>

class McpHost : public QObject
{
    Q_OBJECT
public:
    explicit McpHost(QObject *parent = nullptr);

    QString configPath() const { return m_path; }
    void setConfigPath(const QString &path);

    bool load();
    bool save() const;

    QList<McpServerConfig> configs() const { return m_configs; }
    void upsertServer(const McpServerConfig &cfg);
    void removeServer(const QString &name);
    void setEnabled(const QString &name, bool enabled);
    void replaceConfigs(const QList<McpServerConfig> &cfgs);

    void startEnabled();
    void stopAll();
    void restart(const QString &name);

    QList<McpTool> allTools() const;
    QJsonArray openaiTools() const;
    McpTool findTool(const QString &exposedName) const;

    void callTool(const QString &exposedName, const QJsonObject &args,
                  const std::function<void(QJsonValue result, QString error)> &cb);

    McpClient *client(const QString &name) const;
    QString combinedLog() const;
    int connectedCount() const;

signals:
    void serversChanged();
    void toolsChanged();
    void logUpdated();

private:
    void rebuildClients();
    void disambiguateNames();

    void importIfMissing();

    QString m_path;
    QJsonObject m_document;
    QList<McpServerConfig> m_configs;
    QList<McpClient *> m_clients;
};
