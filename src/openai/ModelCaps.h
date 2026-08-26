#pragma once

#include "openai/ChatTypes.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

ModelCaps capsFromModelId(const QString &id);
ModelCaps capsFromOllamaShow(const QByteArray &json);
QUrl ollamaShowUrl(const QString &baseUrl);
