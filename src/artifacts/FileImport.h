#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Turning a file that is added to a project into plain text the model can read.
// Project files are preloaded into every chat as text, so anything that is not
// text (or cannot be converted to it) has to be refused when it is added, not
// stored and pasted into the prompt later as garbage.
namespace FileImport
{
enum class Plan
{
    Copy,    // already text: store it as it is
    Convert, // Word/OpenDocument or a spreadsheet (LibreOffice), or a PDF (pdftotext)
    Refuse   // an image, archive, executable...: not text and not convertible
};

struct Classification
{
    Plan plan = Plan::Refuse;
    QString refusal; // worded for the user; set when plan is Refuse
};

struct Item
{
    QString filename; // what to store it as
    QByteArray data;  // UTF-8 text
};

struct Result
{
    QList<Item> items; // a spreadsheet gives one item per sheet
    QString error;     // worded for the user; set when nothing could be produced
};

// `pdftotextOverride` is the user's path to pdftotext, if any. A PDF is only
// convertible while a usable pdftotext is found; without one it is refused here,
// straight away, with what to install.
Classification classify(const QString &path, const QString &pdftotextOverride = {});

// For Plan::Convert. Runs LibreOffice or pdftotext and can take a minute or more,
// so call it off the UI thread. Thread-safe.
//   report.docx -> report.docx.txt
//   paper.pdf   -> paper.pdf.txt
//   sales.xlsx  -> sales-<sheet>.csv, one per sheet
Result convert(const QString &path, const QString &officeOverride, const QString &pdftotextOverride = {});
} // namespace FileImport
