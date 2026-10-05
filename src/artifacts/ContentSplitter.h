#pragma once

#include "openai/ChatTypes.h"

#include <QString>
#include <QVector>

class ContentSplitter
{
public:
    static QVector<ContentPart> split(const QString &content);
    // Markdown as it should be handed to the renderer: a single "~" is escaped
    // so it stays a tilde. The renderer reads a pair of them as strike-through,
    // which mangles "about" figures ("~50 V ... ~57 V"). "~~text~~" is left as
    // strike-through, and inline code and URLs are left alone.
    static QString escapeLoneTildes(const QString &markdown);
};
