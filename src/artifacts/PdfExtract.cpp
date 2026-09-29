#include "artifacts/PdfExtract.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

namespace
{
// Callers cut what they use to far less than this; it only stops a huge PDF
// from filling memory before they do.
constexpr qint64 kMaxTextBytes = 4 * 1024 * 1024;
constexpr int kTimeoutMs = 60000;

bool isUsableBinary(const QString &path)
{
    const QFileInfo fi(path);
    return fi.exists() && fi.isFile() && fi.isExecutable();
}

QString expandUserPath(QString p)
{
    p = p.trimmed();
    if (p.startsWith(QLatin1String("file:")))
        p = QUrl(p).toLocalFile();
    if (p.startsWith(QLatin1Char('~')) && (p.size() == 1 || p.at(1) == QLatin1Char('/') || p.at(1) == QLatin1Char('\\')))
        p = QDir::homePath() + p.mid(1);
    return p;
}
} // namespace

QString PdfExtract::missingToolMessage(const QString &overridePath)
{
    const QString set = overridePath.trimmed();
    if (!set.isEmpty())
        return QStringLiteral("No usable pdftotext at %1. Check the path in Settings → Advanced.").arg(set);
    return QStringLiteral(
        "Reading PDFs needs pdftotext (poppler-utils). Install it (Debian/Ubuntu: sudo apt install poppler-utils), "
        "or set its path in Settings → Advanced, and try again.");
}

bool PdfExtract::isPdfPath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0;
}

QString PdfExtract::pdftotextPath(const QString &overridePath)
{
    const QString user = expandUserPath(overridePath);
    if (!user.isEmpty())
    {
        if (isUsableBinary(user))
            return QFileInfo(user).absoluteFilePath();
        // A folder holding it (".../bin") is fine too.
        const QFileInfo fi(user);
        if (fi.isDir())
        {
            const QString inDir = QDir(user).filePath(QStringLiteral("pdftotext"));
            if (isUsableBinary(inDir))
                return QFileInfo(inDir).absoluteFilePath();
        }
        return {};
    }
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
    if (!onPath.isEmpty())
        return onPath;
    // A desktop launcher's PATH is often short (macOS especially).
    return QStandardPaths::findExecutable(QStringLiteral("pdftotext"),
                                          {QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin"),
                                           QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/opt/local/bin"),
                                           QStringLiteral("/snap/bin")});
}

bool PdfExtract::available(const QString &overridePath)
{
    return !pdftotextPath(overridePath).isEmpty();
}

QString PdfExtract::extract(const QString &path, const QString &overridePath, QString *error)
{
    const auto fail = [error](const QString &msg) -> QString
    {
        if (error)
            *error = msg;
        return {};
    };
    if (!QFileInfo::exists(path))
        return fail(QStringLiteral("File not found."));
    const QString bin = pdftotextPath(overridePath);
    if (bin.isEmpty())
        return fail(missingToolMessage(overridePath));

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(bin, {QStringLiteral("-layout"), QStringLiteral("-enc"), QStringLiteral("UTF-8"), path,
                     QStringLiteral("-")});
    if (!proc.waitForStarted(10000))
        return fail(QStringLiteral("Could not start pdftotext: %1").arg(proc.errorString()));

    QByteArray out;
    bool cutOff = false;
    QElapsedTimer clock;
    clock.start();
    // Read as it is produced, so the output can be capped instead of waited for.
    while (true)
    {
        proc.waitForReadyRead(200);
        out += proc.readAllStandardOutput();
        if (out.size() > kMaxTextBytes)
        {
            cutOff = true;
            proc.kill();
            break;
        }
        if (proc.state() == QProcess::NotRunning)
            break;
        if (clock.elapsed() > kTimeoutMs)
        {
            proc.kill();
            proc.waitForFinished(3000);
            return fail(QStringLiteral("Reading the PDF took too long."));
        }
    }
    proc.waitForFinished(3000);
    out += proc.readAllStandardOutput();
    if (out.size() > kMaxTextBytes)
    {
        out.truncate(kMaxTextBytes);
        cutOff = true;
    }

    if (!cutOff && (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0))
    {
        const QStringList lines = QString::fromUtf8(proc.readAllStandardError()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QString why = lines.isEmpty() ? QStringLiteral("pdftotext failed (exit %1).").arg(proc.exitCode())
                                      : lines.first().trimmed();
        why.remove(QStringLiteral("Command Line Error: "));
        return fail(why);
    }

    QString text = QString::fromUtf8(out);
    text.replace(QLatin1Char('\f'), QStringLiteral("\n\n")); // page breaks
    text.replace(QLatin1Char('\r'), QString());
    static const QRegularExpression manyNewlines(QStringLiteral("\n{3,}"));
    text.replace(manyNewlines, QStringLiteral("\n\n"));
    text = text.trimmed();
    if (text.isEmpty())
        return fail(QStringLiteral("No text found in the PDF. A scanned document has to be run through OCR first."));
    if (cutOff)
        text += QStringLiteral("\n\n[the rest of the PDF is not included]");
    if (error)
        error->clear();
    return text;
}
