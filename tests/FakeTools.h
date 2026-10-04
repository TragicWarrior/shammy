#pragma once

#include <QFile>
#include <QString>

// Stand-ins for the optional tools Shammy runs, so tests do not depend on what
// is installed. What each does depends on the input file's name:
//   soffice    "broken" fails, "slow" takes a second. A spreadsheet gives two
//              sheets that are both called "Data"; anything else a document of
//              two paragraphs.
//   pdftotext  "locked" fails (wrong password), "scanned" has no text, "args"
//              echoes its arguments, "huge" writes 6 MB, "endless" never stops.
namespace FakeTools
{
inline const char *soffice = R"(#!/bin/sh
out=""; conv=""; last=""
while [ $# -gt 0 ]; do
  case "$1" in
    --convert-to) conv="$2"; shift ;;
    --outdir) out="$2"; shift ;;
    -env:*|--*) ;;
    *) last="$1" ;;
  esac
  shift
done
case "$last" in *broken*) echo "conversion exploded" >&2; exit 1 ;; esac
case "$last" in *slow*) sleep 1 ;; esac
base=$(basename "$last"); stem="${base%.*}"
case "$conv" in
  *StarCalc*)
    printf '%s' '<html><body><h1>Sheet 1: <em>Data</em></h1><table><tr><td>name</td><td>qty</td></tr><tr><td>apple</td><td>3</td></tr></table><h1>Sheet 2: <em>Data</em></h1><table><tr><td>x</td></tr></table></body></html>' > "$out/$stem.html" ;;
  *)
    printf '%s' '<html><body><p>Hello from the document</p><p>Second paragraph</p></body></html>' > "$out/$stem.html" ;;
esac
)";

inline const char *pdftotext = R"(#!/bin/sh
orig="$*"; file=""
while [ $# -gt 0 ]; do
  case "$1" in
    -enc) shift ;;
    -layout|-) ;;
    *) file="$1" ;;
  esac
  shift
done
case "$file" in
  *locked*) echo "Command Line Error: Incorrect password" >&2; exit 1 ;;
  *scanned*) exit 0 ;;
  *args*) echo "$orig"; exit 0 ;;
  *huge*) yes "lorem ipsum dolor sit amet lorem ipsum" | head -c 6000000; exit 0 ;;
  *endless*) exec yes "lorem ipsum dolor sit amet lorem ipsum" ;;
esac
printf 'Quarterly Report\n\n\n\nNorth   120 135\f\nSecond page text\n'
)";

// Writes `script` to `path` and makes it executable.
inline bool install(const QString &path, const char *script)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(script);
    f.close();
    return QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner);
}
} // namespace FakeTools
