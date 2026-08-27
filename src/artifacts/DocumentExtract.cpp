#include "artifacts/DocumentExtract.h"
#include "artifacts/DocxExport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QUrl>

bool DocumentExtract::isDocumentPath(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("docx") || s == QLatin1String("doc") || s == QLatin1String("docm")
        || s == QLatin1String("dot") || s == QLatin1String("dotx") || s == QLatin1String("dotm")
        || s == QLatin1String("odt") || s == QLatin1String("ott") || s == QLatin1String("fodt")
        || s == QLatin1String("rtf");
}

QString DocumentExtract::textFromWriterHtml(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    QString t = doc.toPlainText();
    t.replace(QLatin1Char('\r'), QString());
    static const QRegularExpression manyNewlines(QStringLiteral("\n{3,}"));
    t.replace(manyNewlines, QStringLiteral("\n\n"));
    return t.trimmed();
}

QString DocumentExtract::extract(const QString &path, const QString &officeOverride, QString *error)
{
    const auto fail = [error](const QString &msg) -> QString
    {
        if (error)
        {
            *error = msg;
        }
        return {};
    };
    if (!isDocumentPath(path))
    {
        return fail(QStringLiteral("Not a Word or OpenDocument text file."));
    }
    if (!QFileInfo::exists(path))
    {
        return fail(QStringLiteral("File not found."));
    }
    const QString bin = DocxExport::sofficePath(officeOverride);
    if (bin.isEmpty())
    {
        return fail(QStringLiteral(
            "LibreOffice or OpenOffice is needed to read Word/OpenDocument files. "
            "Install it, or set the soffice path in Settings → Advanced."));
    }

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        return fail(QStringLiteral("Could not create a temp directory for document extract."));
    }
    const QString inCopy = tmp.filePath(QFileInfo(path).fileName());
    QFile::remove(inCopy);
    if (!QFile::copy(path, inCopy))
    {
        return fail(QStringLiteral("Could not read %1").arg(path));
    }

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
        QStringLiteral("html:HTML (StarWriter)"),
        QStringLiteral("--outdir"),
        tmp.path(),
        inCopy,
    };
    proc.start(bin, args);
    if (!proc.waitForStarted(10000))
    {
        return fail(QStringLiteral("Could not start LibreOffice: %1").arg(proc.errorString()));
    }
    if (!proc.waitForFinished(90000))
    {
        proc.kill();
        proc.waitForFinished(3000);
        return fail(QStringLiteral("LibreOffice timed out converting the document."));
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        const QString out = QString::fromUtf8(proc.readAll()).trimmed();
        if (!out.isEmpty())
        {
            return fail(out);
        }
        return fail(QStringLiteral("LibreOffice failed (exit %1).").arg(proc.exitCode()));
    }

    QString htmlPath = tmp.filePath(QFileInfo(inCopy).completeBaseName() + QStringLiteral(".html"));
    if (!QFileInfo::exists(htmlPath))
    {
        const QFileInfoList found = QDir(tmp.path()).entryInfoList({QStringLiteral("*.html")}, QDir::Files);
        if (!found.isEmpty())
        {
            htmlPath = found.first().absoluteFilePath();
        }
    }
    if (!QFileInfo::exists(htmlPath))
    {
        return fail(QStringLiteral("LibreOffice did not write HTML for the document."));
    }

    QFile hf(htmlPath);
    if (!hf.open(QIODevice::ReadOnly))
    {
        return fail(hf.errorString());
    }
    const QString text = textFromWriterHtml(QString::fromUtf8(hf.readAll()));
    if (text.isEmpty())
    {
        return fail(QStringLiteral("No text found in the document."));
    }
    if (error)
    {
        error->clear();
    }
    return text;
}
