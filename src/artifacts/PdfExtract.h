#pragma once

#include <QString>

// Reading the text out of a PDF with `pdftotext` (poppler-utils), when it is
// installed: an optional tool, like LibreOffice, that is found on PATH or in a
// common install location, or at a path the user has set (a binary, or the
// folder it is in). PDFs are supported only while one is found. Layout is kept
// (`-layout`), so tables stay aligned.
// A scanned PDF has no text to read and is reported as such; OCR is not done.
namespace PdfExtract
{
bool isPdfPath(const QString &path);

// The pdftotext binary, or an empty string if there is none. A user-set path
// (`overridePath`) is used on its own: if it is not usable there is no fallback
// to a detected one, so a wrong setting is noticed rather than hidden.
QString pdftotextPath(const QString &overridePath = {});
bool available(const QString &overridePath = {});
// What to tell the user when there is none.
QString missingToolMessage(const QString &overridePath = {});

// Blocking (a big PDF can take a while, up to a minute): call it off the UI
// thread when it can be. Thread-safe. On failure returns an empty string and
// sets *error to a message worded for the user.
QString extract(const QString &path, const QString &overridePath, QString *error);
} // namespace PdfExtract
