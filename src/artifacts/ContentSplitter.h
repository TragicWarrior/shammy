#pragma once

#include "openai/ChatTypes.h"

#include <QString>
#include <QVector>

class ContentSplitter
{
public:
    static QVector<ContentPart> split(const QString &content);
};
