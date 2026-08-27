#include "artifacts/Attach.h"
#include "artifacts/DocumentExtract.h"
#include "artifacts/SpreadsheetExtract.h"

#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

namespace Attach
{
namespace
{
bool isKnownBinaryMime(const QString &mime)
{
    if (mime.startsWith(QLatin1String("audio/")) || mime.startsWith(QLatin1String("video/"))
        || mime.startsWith(QLatin1String("font/")))
    {
        return true;
    }
    static const QStringList exact = {
        QStringLiteral("application/pdf"),
        QStringLiteral("application/zip"),
        QStringLiteral("application/gzip"),
        QStringLiteral("application/x-gzip"),
        QStringLiteral("application/x-tar"),
        QStringLiteral("application/x-7z-compressed"),
        QStringLiteral("application/x-rar"),
        QStringLiteral("application/x-bzip2"),
        QStringLiteral("application/x-xz"),
        QStringLiteral("application/x-executable"),
        QStringLiteral("application/x-sharedlib"),
        QStringLiteral("application/x-elf"),
        QStringLiteral("application/x-msdownload"),
        QStringLiteral("application/vnd.microsoft.portable-executable"),
        QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation"),
        QStringLiteral("application/vnd.ms-powerpoint"),
        QStringLiteral("application/vnd.oasis.opendocument.presentation"),
        QStringLiteral("application/epub+zip"),
        QStringLiteral("application/x-iso9660-image"),
        QStringLiteral("application/wasm"),
    };
    return exact.contains(mime);
}
} // namespace

bool isTextMime(const QString &mime)
{
    if (mime.startsWith(QLatin1String("text/")))
    {
        return true;
    }
    if (mime.endsWith(QLatin1String("+xml")) || mime.endsWith(QLatin1String("+json")))
    {
        return true;
    }
    static const QStringList extra = {
        QStringLiteral("application/json"),
        QStringLiteral("application/xml"),
        QStringLiteral("application/javascript"),
        QStringLiteral("application/x-javascript"),
        QStringLiteral("application/typescript"),
        QStringLiteral("application/yaml"),
        QStringLiteral("application/x-yaml"),
        QStringLiteral("application/toml"),
        QStringLiteral("application/x-sh"),
        QStringLiteral("application/x-shellscript"),
        QStringLiteral("application/sql"),
        QStringLiteral("application/x-sql"),
        QStringLiteral("application/graphql"),
        QStringLiteral("application/x-ndjson"),
        QStringLiteral("inode/x-empty"),
        QStringLiteral("application/x-empty"),
    };
    return extra.contains(mime);
}

bool looksLikeText(const QByteArray &head)
{
    if (head.isEmpty())
    {
        return true;
    }
    if (head.contains('\0'))
    {
        return false;
    }
    int ok = 0;
    for (unsigned char c : head)
    {
        if (c >= 0x20 || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x1b || c >= 0x80)
        {
            ++ok;
        }
    }
    return ok * 2 >= head.size();
}

Kind kindForMime(const QString &mime, const QString &suffix, const QByteArray &head)
{
    const QString suf = suffix.toLower();
    if (!suf.isEmpty() && SpreadsheetExtract::isSpreadsheetPath(QStringLiteral("x.") + suf))
    {
        return Kind::Spreadsheet;
    }
    if (!suf.isEmpty() && DocumentExtract::isDocumentPath(QStringLiteral("x.") + suf))
    {
        return Kind::Document;
    }
    if (mime.startsWith(QLatin1String("image/")))
    {
        return Kind::Image;
    }
    if (mime == QLatin1String("application/msword")
        || mime
            == QLatin1String(
                "application/vnd.openxmlformats-officedocument.wordprocessingml.document")
        || mime == QLatin1String("application/vnd.oasis.opendocument.text")
        || mime == QLatin1String("application/rtf") || mime == QLatin1String("text/rtf"))
    {
        return Kind::Document;
    }
    if (isTextMime(mime))
    {
        return Kind::Text;
    }
    if (isKnownBinaryMime(mime))
    {
        return Kind::Unsupported;
    }
    if (looksLikeText(head))
    {
        return Kind::Text;
    }
    return Kind::Unsupported;
}

Kind kindForPath(const QString &path)
{
    if (SpreadsheetExtract::isSpreadsheetPath(path))
    {
        return Kind::Spreadsheet;
    }
    if (DocumentExtract::isDocumentPath(path))
    {
        return Kind::Document;
    }
    const QFileInfo fi(path);
    const QString mime = QMimeDatabase().mimeTypeForFile(fi).name();
    if (mime.startsWith(QLatin1String("image/")))
    {
        return Kind::Image;
    }
    if (isTextMime(mime))
    {
        return Kind::Text;
    }
    QByteArray head;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
    {
        head = f.read(4096);
    }
    return kindForMime(mime, fi.suffix(), head);
}

QString pastedChipLabel(int nChars)
{
    const int n = qMax(0, nChars);
    QString size;
    if (n < 1000)
    {
        size = QString::number(n);
    }
    else if (n < 9950)
    {
        size = QString::number(n / 1000.0, 'f', 1);
        if (size.endsWith(QLatin1String(".0")))
        {
            size.chop(2);
        }
        size += QLatin1Char('k');
    }
    else if (n < 999950)
    {
        size = QString::number((n + 500) / 1000) + QLatin1Char('k');
    }
    else
    {
        size = QString::number(n / 1000000.0, 'f', 1);
        if (size.endsWith(QLatin1String(".0")))
        {
            size.chop(2);
        }
        size += QLatin1Char('M');
    }
    return QStringLiteral("[pasted text %1]").arg(size);
}
} // namespace Attach
