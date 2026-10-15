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

// Ollama refuses a request outright when it asks for something its template for
// the model lacks ("<model> does not support tools"), which reads as if the
// model could not. This says what is going on and what to change in Shammy,
// keeping Ollama's own words; any other error comes back unchanged.
QString explainServerError(const QString &error, const QString &backendName, const QString &model);
int loadedContextFromOllamaPs(const QByteArray &json, const QString &model);
