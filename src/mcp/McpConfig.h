#pragma once

#include "openai/ChatTypes.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace McpConfig
{

QString defaultPath();
QStringList importCandidates();
QList<McpServerConfig> parseServers(const QJsonObject &root);
QJsonObject mergeServers(QJsonObject root, const QList<McpServerConfig> &cfgs);
QJsonObject mcpServersOnly(const QJsonObject &root);
bool readJsonFile(const QString &path, QJsonObject *out);

} // namespace McpConfig
