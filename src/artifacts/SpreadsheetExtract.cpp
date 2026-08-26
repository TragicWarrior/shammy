#include "artifacts/SpreadsheetExtract.h"
#include "artifacts/DocxExport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>

static QString decodeEntities(QString s)
{
    s.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    s.replace(QLatin1String("&amp;"), QLatin1String("&"));
    s.replace(QLatin1String("&lt;"), QLatin1String("<"));
    s.replace(QLatin1String("&gt;"), QLatin1String(">"));
    s.replace(QLatin1String("&quot;"), QLatin1String("\""));
    s.replace(QLatin1String("&#39;"), QLatin1String("'"));
    s.replace(QLatin1String("&apos;"), QLatin1String("'"));
    return s.trimmed();
}

static QString csvField(QString s)
{
    s.replace(QLatin1Char('\r'), QString());
    const bool quote = s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"'))
        || s.contains(QLatin1Char('\n'));
    if (quote)
    {
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + s + QLatin1Char('"');
    }
    return s;
}

static QString cellText(QString inner)
{
    inner.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QString());
    return decodeEntities(inner);
}

static QString tableToCsv(const QString &tableHtml)
{
    static const QRegularExpression rowRe(QStringLiteral("<tr\\b[^>]*>([\\s\\S]*?)</tr>"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cellRe(QStringLiteral("<t[dh]\\b[^>]*>([\\s\\S]*?)</t[dh]>"),
                                           QRegularExpression::CaseInsensitiveOption);
    QStringList lines;
    QRegularExpressionMatchIterator rows = rowRe.globalMatch(tableHtml);
    while (rows.hasNext())
    {
        const QString row = rows.next().captured(1);
        QStringList fields;
        QRegularExpressionMatchIterator cells = cellRe.globalMatch(row);
        while (cells.hasNext())
            fields.append(csvField(cellText(cells.next().captured(1))));
        if (!fields.isEmpty())
            lines.append(fields.join(QLatin1Char(',')));
    }
    return lines.join(QLatin1Char('\n'));
}

bool SpreadsheetExtract::isSpreadsheetPath(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("xlsx") || s == QLatin1String("xlsm") || s == QLatin1String("xls")
        || s == QLatin1String("xltx") || s == QLatin1String("xltm") || s == QLatin1String("xlt")
        || s == QLatin1String("ods") || s == QLatin1String("ots") || s == QLatin1String("fods");
}

QString SpreadsheetExtract::safeSheetFileName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    bool dash = false;
    for (const QChar c : name.trimmed())
    {
        if (c.isLetterOrNumber())
        {
            out += c;
            dash = false;
        }
        else if (!dash && !out.isEmpty())
        {
            out += QLatin1Char('-');
            dash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    if (out.isEmpty())
        out = QStringLiteral("sheet");
    if (out.size() > 40)
        out.truncate(40);
    return out;
}

QVector<SpreadsheetExtract::Sheet> SpreadsheetExtract::sheetsFromCalcHtml(const QString &html)
{
    static const QRegularExpression tokenRe(
        QStringLiteral("(?:<h1\\b[^>]*>\\s*Sheet\\s+\\d+:\\s*<em>([^<]*)</em>\\s*</h1>)"
                       "|(<table\\b[\\s\\S]*?</table>)"),
        QRegularExpression::CaseInsensitiveOption);
    QVector<Sheet> out;
    QString pending;
    QRegularExpressionMatchIterator it = tokenRe.globalMatch(html);
    while (it.hasNext())
    {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedStart(1) >= 0)
        {
            pending = decodeEntities(m.captured(1));
            continue;
        }
        const QString csv = tableToCsv(m.captured(2));
        if (csv.trimmed().isEmpty())
        {
            pending.clear();
            continue;
        }
        Sheet sh;
        sh.name = pending.isEmpty() ? QStringLiteral("Sheet%1").arg(out.size() + 1) : pending;
        sh.csv = csv;
        pending.clear();
        out.append(sh);
    }
    return out;
}

QVector<SpreadsheetExtract::Sheet> SpreadsheetExtract::extract(const QString &path,
                                                              const QString &officeOverride,
                                                              QString *error)
{
    const auto fail = [error](const QString &msg) -> QVector<Sheet>
    {
        if (error)
            *error = msg;
        return {};
    };
    if (!isSpreadsheetPath(path))
        return fail(QStringLiteral("Not a spreadsheet."));
    if (!QFileInfo::exists(path))
        return fail(QStringLiteral("File not found."));
    const QString bin = DocxExport::sofficePath(officeOverride);
    if (bin.isEmpty())
    {
        return fail(QStringLiteral(
            "LibreOffice or OpenOffice is needed to read Excel/OpenDocument spreadsheets. "
            "Install it, or set the soffice path in Settings → Advanced."));
    }

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return fail(QStringLiteral("Could not create a temp directory for spreadsheet extract."));
    const QString inCopy = tmp.filePath(QFileInfo(path).fileName());
    QFile::remove(inCopy);
    if (!QFile::copy(path, inCopy))
        return fail(QStringLiteral("Could not read %1").arg(path));

    const QString profile = tmp.filePath(QStringLiteral("lo-profile"));
    QDir().mkpath(profile);
    const QString profileUrl = QUrl::fromLocalFile(profile).toString();

    QProcess proc;
    proc.setWorkingDirectory(tmp.path());
    proc.setProcessChannelMode(QProcess::MergedChannels);
    const QStringList args = {
        QStringLiteral("--headless"),
        QStringLiteral("--nologo"),
        QStringLiteral("--nofirststartwizard"),
        QStringLiteral("--norestore"),
        QStringLiteral("--nolockcheck"),
        QStringLiteral("-env:UserInstallation=%1").arg(profileUrl),
        QStringLiteral("--convert-to"),
        QStringLiteral("html:HTML (StarCalc)"),
        QStringLiteral("--outdir"),
        tmp.path(),
        inCopy,
    };
    proc.start(bin, args);
    if (!proc.waitForStarted(10000))
        return fail(QStringLiteral("Could not start LibreOffice: %1").arg(proc.errorString()));
    if (!proc.waitForFinished(90000))
    {
        proc.kill();
        proc.waitForFinished(3000);
        return fail(QStringLiteral("LibreOffice timed out converting the spreadsheet."));
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        const QString out = QString::fromUtf8(proc.readAll()).trimmed();
        if (!out.isEmpty())
            return fail(out);
        return fail(QStringLiteral("LibreOffice failed (exit %1).").arg(proc.exitCode()));
    }

    QString htmlPath = tmp.filePath(QFileInfo(inCopy).completeBaseName() + QStringLiteral(".html"));
    if (!QFileInfo::exists(htmlPath))
    {
        const QFileInfoList found = QDir(tmp.path()).entryInfoList({QStringLiteral("*.html")}, QDir::Files);
        if (!found.isEmpty())
            htmlPath = found.first().absoluteFilePath();
    }
    if (!QFileInfo::exists(htmlPath))
        return fail(QStringLiteral("LibreOffice did not write HTML for the spreadsheet."));

    QFile hf(htmlPath);
    if (!hf.open(QIODevice::ReadOnly))
        return fail(hf.errorString());
    const QString html = QString::fromUtf8(hf.readAll());
    const QVector<Sheet> sheets = sheetsFromCalcHtml(html);
    if (sheets.isEmpty())
        return fail(QStringLiteral("No sheets found in the spreadsheet."));
    if (error)
        error->clear();
    return sheets;
}
