#include "artifacts/DocxExport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

static bool isUsableBinary(const QString &path)
{
    const QFileInfo fi(path);
    return fi.exists() && fi.isFile() && fi.isExecutable();
}

static QString firstUsableInDir(const QString &dir)
{
    static const QStringList names = {
        QStringLiteral("soffice"),
        QStringLiteral("soffice.bin"),
        QStringLiteral("soffice.exe"),
        QStringLiteral("soffice.com"),
        QStringLiteral("libreoffice"),
        QStringLiteral("libreoffice.exe"),
        QStringLiteral("ooffice"),
        QStringLiteral("ooffice.exe"),
    };
    const QDir d(dir);
    if (!d.exists())
        return {};
    for (const QString &n : names)
    {
        const QString p = d.filePath(n);
        if (isUsableBinary(p))
            return QFileInfo(p).absoluteFilePath();
    }
    return {};
}

static QString resolveFromLocation(const QString &path)
{
    if (path.isEmpty())
        return {};
    if (isUsableBinary(path))
        return QFileInfo(path).absoluteFilePath();
    const QFileInfo fi(path);
    if (!fi.exists())
        return {};
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    const QStringList sub = {
        dir,
        dir + QStringLiteral("/program"),
        dir + QStringLiteral("/bin"),
        dir + QStringLiteral("/Contents/MacOS"),
    };
    for (const QString &d : sub)
    {
        const QString found = firstUsableInDir(d);
        if (!found.isEmpty())
            return found;
    }
    return {};
}

static QString expandUserPath(QString p)
{
    p = p.trimmed();
    if (p.startsWith(QLatin1String("file:")))
        p = QUrl(p).toLocalFile();
    if (p.startsWith(QLatin1Char('~'))
        && (p.size() == 1 || p.at(1) == QLatin1Char('/') || p.at(1) == QLatin1Char('\\')))
        p = QDir::homePath() + p.mid(1);
    return p;
}

static QString missingOfficeMessage(const QString &overridePath)
{
    const QString trimmed = overridePath.trimmed();
    if (!trimmed.isEmpty())
        return QStringLiteral("No usable LibreOffice or OpenOffice binary at %1.").arg(trimmed);
    return QStringLiteral(
        "LibreOffice or OpenOffice was not found. Install it, or set the soffice path in Settings → Advanced.");
}

QString DocxExport::sofficePath(const QString &overridePath)
{
    const QString user = expandUserPath(overridePath);
    if (!user.isEmpty())
        return resolveFromLocation(user);

    static const QStringList names = {
        QStringLiteral("soffice"),
        QStringLiteral("libreoffice"),
        QStringLiteral("ooffice"),
        QStringLiteral("soffice.exe"),
        QStringLiteral("libreoffice.exe"),
    };
    for (const QString &name : names)
    {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }

    QStringList extras = {
        QStringLiteral("/usr/bin/soffice"),
        QStringLiteral("/usr/bin/libreoffice"),
        QStringLiteral("/usr/bin/ooffice"),
        QStringLiteral("/usr/lib/libreoffice/program/soffice"),
        QStringLiteral("/usr/lib/openoffice/program/soffice"),
        QStringLiteral("/snap/bin/libreoffice"),
        QStringLiteral("/snap/bin/soffice"),
        QStringLiteral("/opt/openoffice4/program/soffice"),
        QStringLiteral("/Applications/LibreOffice.app/Contents/MacOS/soffice"),
        QStringLiteral("/Applications/OpenOffice.app/Contents/MacOS/soffice"),
        QStringLiteral("C:/Program Files/LibreOffice/program/soffice.exe"),
        QStringLiteral("C:/Program Files/LibreOffice/program/soffice.com"),
        QStringLiteral("C:/Program Files (x86)/LibreOffice/program/soffice.exe"),
        QStringLiteral("C:/Program Files/OpenOffice 4/program/soffice.exe"),
        QStringLiteral("C:/Program Files (x86)/OpenOffice 4/program/soffice.exe"),
    };
    QDir opt(QStringLiteral("/opt"));
    if (opt.exists())
    {
        const QStringList dirs = opt.entryList({QStringLiteral("libreoffice*"), QStringLiteral("openoffice*")},
                                               QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : dirs)
            extras << opt.filePath(name) + QStringLiteral("/program/soffice");
    }
    for (const QString &p : extras)
    {
        const QString found = resolveFromLocation(p);
        if (!found.isEmpty())
            return found;
    }
    return {};
}

bool DocxExport::available(const QString &overridePath)
{
    return !sofficePath(overridePath).isEmpty();
}

QString DocxExport::fileNameFromTitle(const QString &title)
{
    QString s = title.trimmed();
    if (s.endsWith(QLatin1String(".docx"), Qt::CaseInsensitive))
        s.chop(5);
    else if (s.endsWith(QLatin1String(".doc"), Qt::CaseInsensitive))
        s.chop(4);
    if (s.isEmpty())
        s = QStringLiteral("artifact");
    QString out;
    out.reserve(s.size());
    bool dash = false;
    for (const QChar c : s)
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
        out = QStringLiteral("artifact");
    if (out.size() > 80)
        out.truncate(80);
    if (!out.endsWith(QLatin1String(".docx"), Qt::CaseInsensitive))
        out += QStringLiteral(".docx");
    return out;
}

QString DocxExport::convertHtmlToDocx(const QString &html, const QString &destPath,
                                     const QString &overridePath)
{
    if (html.trimmed().isEmpty())
        return QStringLiteral("Nothing to export to Word.");
    QString dest = destPath;
    if (dest.startsWith(QLatin1String("file:")))
        dest = QUrl(dest).toLocalFile();
    if (dest.isEmpty())
        return QStringLiteral("No destination file.");
    const QString bin = sofficePath(overridePath);
    if (bin.isEmpty())
        return missingOfficeMessage(overridePath);

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return QStringLiteral("Could not create a temp directory for Word export.");
    const QString inPath = tmp.filePath(QStringLiteral("artifact.html"));
    QFile in(inPath);
    if (!in.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return in.errorString();
    in.write(html.toUtf8());
    in.close();

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
        QStringLiteral("docx:Office Open XML Text"),
        QStringLiteral("--outdir"),
        tmp.path(),
        inPath,
    };
    proc.start(bin, args);
    if (!proc.waitForStarted(10000))
        return QStringLiteral("Could not start LibreOffice: %1").arg(proc.errorString());
    if (!proc.waitForFinished(90000))
    {
        proc.kill();
        proc.waitForFinished(3000);
        return QStringLiteral("LibreOffice timed out converting to Word.");
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        const QString out = QString::fromUtf8(proc.readAll()).trimmed();
        if (!out.isEmpty())
            return out;
        return QStringLiteral("LibreOffice failed (exit %1).").arg(proc.exitCode());
    }

    QString produced = tmp.filePath(QStringLiteral("artifact.docx"));
    if (!QFileInfo::exists(produced))
    {
        const QFileInfoList found = QDir(tmp.path()).entryInfoList({QStringLiteral("*.docx")}, QDir::Files);
        if (!found.isEmpty())
            produced = found.first().absoluteFilePath();
    }
    if (!QFileInfo::exists(produced))
    {
        const QString log = QString::fromUtf8(proc.readAll()).trimmed();
        if (!log.isEmpty())
            return log;
        return QStringLiteral("LibreOffice did not write a .docx file.");
    }

    QDir().mkpath(QFileInfo(dest).absolutePath());
    QFile::remove(dest);
    if (!QFile::copy(produced, dest))
        return QStringLiteral("Could not write %1").arg(dest);
    return {};
}
