#include "artifacts/FileImport.h"
#include "artifacts/DocumentExtract.h"
#include "artifacts/DocxExport.h"
#include "artifacts/PdfExtract.h"
#include "artifacts/SpreadsheetExtract.h"

#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QThread>

#include <memory>

namespace FileImport
{
namespace
{
constexpr qint64 kMinPerFileBytes = 32 * 1024;

const char *kNotText = "that file type isn't text and can't be converted to it.";

QString officeMissing(const QString &overridePath)
{
    const QString set = overridePath.trimmed();
    if (!set.isEmpty())
        return QStringLiteral("no usable LibreOffice or OpenOffice at %1. Check the path in Settings → Advanced.").arg(set);
    return QStringLiteral(
        "reading Word, OpenDocument and spreadsheet files needs LibreOffice or OpenOffice. Install it, or set "
        "the soffice path in Settings → Advanced.");
}
} // namespace

Classification classify(const QString &path, const Tools &tools)
{
    Classification c;
    c.kind = Attach::kindForPath(path);
    switch (c.kind)
    {
    case Attach::Kind::Text:
        c.plan = Plan::Copy;
        break;
    case Attach::Kind::Spreadsheet:
    case Attach::Kind::Document:
        if (DocxExport::available(tools.office))
            c.plan = Plan::Convert;
        else
            c.reason = officeMissing(tools.office);
        break;
    case Attach::Kind::Pdf:
        if (PdfExtract::available(tools.pdftotext))
            c.plan = Plan::Convert;
        else
            c.reason = PdfExtract::missingToolMessage(tools.pdftotext);
        break;
    case Attach::Kind::Image:
        c.reason = QStringLiteral("it's an image, and images can't be converted to text.");
        break;
    case Attach::Kind::Unsupported:
        c.reason = QString::fromUtf8(kNotText);
        break;
    }
    return c;
}

Result convert(const QString &path, const Tools &tools)
{
    Result r;
    const QFileInfo fi(path);
    QString err;
    if (SpreadsheetExtract::isSpreadsheetPath(path))
    {
        const auto sheets = SpreadsheetExtract::extract(path, tools.office, &err);
        if (sheets.isEmpty())
        {
            r.error = err.isEmpty() ? QStringLiteral("it could not be converted.") : err;
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
            r.items.append({name, sh.name, sh.csv.toUtf8()});
        }
        return r;
    }
    if (DocumentExtract::isDocumentPath(path))
    {
        const QString text = DocumentExtract::extract(path, tools.office, &err);
        if (text.isEmpty())
        {
            r.error = err.isEmpty() ? QStringLiteral("it could not be converted.") : err;
            return r;
        }
        r.items.append({fi.fileName() + QStringLiteral(".txt"), {}, text.toUtf8()});
        return r;
    }
    if (PdfExtract::isPdfPath(path))
    {
        const QString text = PdfExtract::extract(path, tools.pdftotext, &err);
        if (text.isEmpty())
        {
            r.error = err.isEmpty() ? QStringLiteral("it could not be read.") : err;
            return r;
        }
        r.items.append({fi.fileName() + QStringLiteral(".txt"), {}, text.toUtf8()});
        return r;
    }
    r.error = QString::fromUtf8(kNotText);
    return r;
}

void convertInBackground(const QString &path, const Tools &tools, QObject *context,
                         std::function<void(const Result &)> done)
{
    auto result = std::make_shared<Result>();
    QThread *thread = QThread::create([result, path, tools]() { *result = convert(path, tools); });
    // `finished` is emitted on the worker thread, so this is queued to
    // `context`'s thread, and Qt drops it if `context` is destroyed first.
    QObject::connect(thread, &QThread::finished, context, [result, done = std::move(done)]() { done(*result); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

qint64 budgetBytes(int contextTokens)
{
    const qint64 share = qint64(qMax(0, contextTokens)) * kBytesPerToken * kContextSharePercent / 100;
    return qBound(kMinBudgetBytes, share, kMaxBudgetBytes);
}

qint64 perFileLimitBytes(qint64 budgetBytes)
{
    return qMin(budgetBytes, qMax(kMinPerFileBytes, budgetBytes / 2));
}

bool cutToFit(QByteArray &data, qint64 maxBytes)
{
    maxBytes = qMax<qint64>(0, maxBytes);
    if (data.size() <= maxBytes)
        return false;
    data.truncate(maxBytes);
    // Step back over a character whose last bytes were cut off.
    int i = data.size();
    int continuation = 0;
    while (i > 0 && continuation < 4 && (static_cast<uchar>(data.at(i - 1)) & 0xC0) == 0x80)
    {
        --i;
        ++continuation;
    }
    if (i > 0)
    {
        const uchar lead = static_cast<uchar>(data.at(i - 1));
        const int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (need > 1 && continuation + 1 < need)
            data.truncate(i - 1);
    }
    return true;
}
} // namespace FileImport
