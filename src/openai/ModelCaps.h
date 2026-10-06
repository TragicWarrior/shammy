#pragma once

#include "openai/ChatTypes.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

ModelCaps capsFromModelId(const QString &id);
ModelCaps capsFromOllamaShow(const QByteArray &json);
QUrl ollamaShowUrl(const QString &baseUrl);

// The context size Ollama loaded `model` with, from an /api/ps reply, or 0 if
// the model is not loaded or the reply does not say. This is the size that is
// really in use: Ollama ignores `num_ctx` on its OpenAI-compatible route, so it
// can be much smaller than the one set here.
QUrl ollamaPsUrl(const QString &baseUrl);
int loadedContextFromOllamaPs(const QByteArray &json, const QString &model);
