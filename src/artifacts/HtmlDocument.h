#pragma once

#include <QString>

class HtmlDocument
{
public:
    static bool isHtmlType(const QString &mime);
    static bool isSvgType(const QString &mime);
    static bool isMarkdownType(const QString &mime);
    static bool isCompleteDocument(const QString &html);
    static bool looksLikeSvg(const QString &content);
    static QString complete(const QString &content, const QString &mime, const QString &title);
};
