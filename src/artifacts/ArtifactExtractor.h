#pragma once

#include "openai/ChatTypes.h"

#include <QString>
#include <QVector>

class ArtifactExtractor
{
public:
    static constexpr int kFencePromoteMinLines = 15;

    static QVector<ArtifactDraft> extract(const QString &content);
    static QString guessMime(const QString &typeOrLang);
};
