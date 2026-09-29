#include "artifacts/FileImport.h"
#include "artifacts/Attach.h"
#include "artifacts/DocumentExtract.h"
#include "artifacts/PdfExtract.h"
#include "artifacts/SpreadsheetExtract.h"

#include <QFileInfo>
#include <QSet>

namespace FileImport
{
namespace
{
QString cantAdd(const QString &path, const QString &why)
{
    return QStringLiteral("Can't add `%1`: %2").arg(QFileInfo(path).fileName(), why);
}
} // namespace

Classification classify(const QString &path, const QString &pdftotextOverride)
{
    Classification c;
    switch (Attach::kindForPath(path))
    {
    case Attach::Kind::Text:
        c.plan = Plan::Copy;
        break;
    case Attach::Kind::Spreadsheet:
    case Attach::Kind::Document:
        c.plan = Plan::Convert;
        break;
    case Attach::Kind::Pdf:
        if (PdfExtract::available(pdftotextOverride))
            c.plan = Plan::Convert;
        else
            c.refusal = cantAdd(path, PdfExtract::missingToolMessage(pdftotextOverride));
        break;
    case Attach::Kind::Image:
        c.refusal = cantAdd(path, QStringLiteral("project files are preloaded as text, so images can't be added."));
        break;
    case Attach::Kind::Unsupported:
        c.refusal = cantAdd(path, QStringLiteral("that file type isn't text and can't be converted to it."));
        break;
    }
    return c;
}

Result convert(const QString &path, const QString &officeOverride, const QString &pdftotextOverride)
{
    Result r;
    const QFileInfo fi(path);
    QString err;
    if (SpreadsheetExtract::isSpreadsheetPath(path))
    {
        const auto sheets = SpreadsheetExtract::extract(path, officeOverride, &err);
        if (sheets.isEmpty())
        {
            r.error = cantAdd(path, err.isEmpty() ? QStringLiteral("it could not be converted.") : err);
            return r;
        }
        // Two sheets can reduce to the same file name ("Q1 Sales" / "Q1-Sales").
        QSet<QString> used;
        for (const SpreadsheetExtract::Sheet &sh : sheets)
        {
            const QString base = fi.completeBaseName() + QLatin1Char('-') + SpreadsheetExtract::safeSheetFileName(sh.name);
            QString name = base + QStringLiteral(".csv");
            for (int n = 2; used.contains(name); ++n)
                name = QStringLiteral("%1-%2.csv").arg(base).arg(n);
            used.insert(name);
            r.items.append({name, sh.csv.toUtf8()});
        }
        return r;
    }
    if (DocumentExtract::isDocumentPath(path))
    {
        const QString text = DocumentExtract::extract(path, officeOverride, &err);
        if (text.isEmpty())
        {
            r.error = cantAdd(path, err.isEmpty() ? QStringLiteral("it could not be converted.") : err);
            return r;
        }
        r.items.append({fi.fileName() + QStringLiteral(".txt"), text.toUtf8()});
        return r;
    }
    if (PdfExtract::isPdfPath(path))
    {
        const QString text = PdfExtract::extract(path, pdftotextOverride, &err);
        if (text.isEmpty())
        {
            r.error = cantAdd(path, err.isEmpty() ? QStringLiteral("it could not be read.") : err);
            return r;
        }
        r.items.append({fi.fileName() + QStringLiteral(".txt"), text.toUtf8()});
        return r;
    }
    r.error = cantAdd(path, QStringLiteral("that file type isn't text and can't be converted to it."));
    return r;
}
} // namespace FileImport
