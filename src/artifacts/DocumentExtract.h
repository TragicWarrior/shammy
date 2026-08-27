#pragma once

#include <QString>

class DocumentExtract
{
public:
    static bool isDocumentPath(const QString &path);
    // Flatten LibreOffice Writer HTML export. Public for tests.
    static QString textFromWriterHtml(const QString &html);
    // Blocking. Empty string on failure; error is filled when provided.
    static QString extract(const QString &path, const QString &officeOverride = {},
                           QString *error = nullptr);
};
