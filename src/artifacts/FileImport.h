#pragma once

#include "artifacts/Attach.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>

class QObject;

// Turning a file into plain text a model can read, for project files and chat
// attachments alike, and how much of that text fits in one prompt. Anything
// that is not text, or cannot be converted to it with a tool that is installed,
// is refused up front with a reason, not stored or sent as garbage.
namespace FileImport
{
enum class Plan
{
    Copy,    // already text: use it as it is
    Convert, // Word/OpenDocument or a spreadsheet (LibreOffice), or a PDF (pdftotext)
    Refuse   // an image, archive, executable..., or no tool to convert it with
};

// The optional converters as the user has set them; empty means auto-detect.
struct Tools
{
    QString office;    // LibreOffice / OpenOffice (soffice)
    QString pdftotext; // poppler-utils
};

struct Classification
{
    Plan plan = Plan::Refuse;
    Attach::Kind kind = Attach::Kind::Unsupported;
    QString reason; // set when plan is Refuse; reads on from "Can't add `name`: "
};

struct Item
{
    QString filename; // what to store it as
    QString title;    // a spreadsheet's sheet name; empty otherwise
    QByteArray data;  // UTF-8 text
};

struct Result
{
    QList<Item> items; // a spreadsheet gives one item per sheet
    QString error;     // set when nothing could be produced; reads on from "Can't add `name`: "
};

// Whether a file can become text, and how. Word/OpenDocument and spreadsheet
// files are convertible only while LibreOffice or OpenOffice is found, PDFs
// only while pdftotext is: without the tool they are refused with what to
// install, straight away.
Classification classify(const QString &path, const Tools &tools = {});

// For Plan::Convert. Runs LibreOffice or pdftotext and can take a minute or
// more, so use convertInBackground() from the UI thread. Thread-safe.
//   report.docx -> report.docx.txt
//   paper.pdf   -> paper.pdf.txt
//   sales.xlsx  -> sales-<sheet>.csv, one per sheet
Result convert(const QString &path, const Tools &tools = {});

// convert() on a worker thread. `done` runs afterwards on `context`'s thread,
// or not at all if `context` has been destroyed by then.
void convertInBackground(const QString &path, const Tools &tools, QObject *context,
                         std::function<void(const Result &)> done);

// How much file text one prompt may carry: kContextSharePercent of the model's
// context window at about kBytesPerToken, between kMinBudgetBytes and
// kMaxBudgetBytes, so there is always room left to talk. Project files use it
// for the system prompt; chat attachments for the message they are sent with.
constexpr int kContextSharePercent = 40;
constexpr int kBytesPerToken = 4;
constexpr qint64 kMinBudgetBytes = 2 * 1024;
constexpr qint64 kMaxBudgetBytes = 256 * 1024;
qint64 budgetBytes(int contextTokens);
// No one file may take more than this much of a budget: half of it, but never
// less than 32 KB (or the whole budget, if that is smaller).
qint64 perFileLimitBytes(qint64 budgetBytes);
// Cuts `data` to at most `maxBytes`, never inside a UTF-8 character. Returns
// whether anything was cut.
bool cutToFit(QByteArray &data, qint64 maxBytes);
} // namespace FileImport
