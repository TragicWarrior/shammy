#pragma once

#include <QString>
#include <QVector>

class SpreadsheetExtract
{
public:
    struct Sheet
    {
        QString name;
        QString csv;
    };

    static bool isSpreadsheetPath(const QString &path);
    static QString safeSheetFileName(const QString &name);
    // Parse LibreOffice Calc HTML export. Public for tests.
    static QVector<Sheet> sheetsFromCalcHtml(const QString &html);
    // Blocking. Empty vector on failure; error is filled when provided.
    static QVector<Sheet> extract(const QString &path, const QString &officeOverride = {},
                                  QString *error = nullptr);
};
