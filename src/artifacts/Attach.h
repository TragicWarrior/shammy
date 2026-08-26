#pragma once

#include <QByteArray>
#include <QString>

namespace Attach
{
enum class Kind
{
    Image,
    Spreadsheet,
    Text,
    Unsupported
};

// Pastes this long or longer become a chip instead of filling the composer.
constexpr int kPasteChipMin = 1000;

Kind kindForPath(const QString &path);
Kind kindForMime(const QString &mime, const QString &suffix = {}, const QByteArray &head = {});
bool looksLikeText(const QByteArray &head);
bool isTextMime(const QString &mime);
QString pastedChipLabel(int nChars);
} // namespace Attach
