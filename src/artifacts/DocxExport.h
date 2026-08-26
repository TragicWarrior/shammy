#pragma once

#include <QString>

class DocxExport
{
public:
    // Empty override auto-detects LibreOffice/OpenOffice. A non-empty override
    // is used exclusively (file, directory, or .app); no fallback if it fails.
    static QString sofficePath(const QString &overridePath = {});
    static bool available(const QString &overridePath = {});
    static QString fileNameFromTitle(const QString &title);
    // Blocking. Empty return is success; otherwise an error string.
    static QString convertHtmlToDocx(const QString &html, const QString &destPath,
                                    const QString &overridePath = {});
};
