#include "artifacts/DocxExport.h"
#include "artifacts/PdfExtract.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

// PdfExtract: finding pdftotext (auto-detected, or at a path the user set, like
// LibreOffice), and turning its output into text. Most cases use a stand-in
// script so they do not depend on what is installed.

namespace
{
// A stand-in for pdftotext. What it does depends on the PDF's file name.
const char *kFakePdftotext = R"(#!/bin/sh
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
} // namespace

class TestPdfExtract : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString m_fake;

    QString pdf(const QString &name)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot write %s", qPrintable(path));
        f.write("%PDF-1.4\n");
        return path;
    }

private slots:
    void initTestCase()
    {
        if (!QFile::exists(QStringLiteral("/bin/sh")))
            QSKIP("needs /bin/sh for the stand-in pdftotext");
        QVERIFY(m_dir.isValid());
        m_fake = m_dir.filePath(QStringLiteral("pdftotext"));
        QFile f(m_fake);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kFakePdftotext);
        f.close();
        QVERIFY(QFile::setPermissions(m_fake, QFile::permissions(m_fake) | QFileDevice::ExeOwner));
    }

    void isPdfPath()
    {
        QVERIFY(PdfExtract::isPdfPath(QStringLiteral("a.pdf")));
        QVERIFY(PdfExtract::isPdfPath(QStringLiteral("/x/y/REPORT.PDF")));
        QVERIFY(!PdfExtract::isPdfPath(QStringLiteral("a.pdf.txt")));
        QVERIFY(!PdfExtract::isPdfPath(QStringLiteral("pdf")));
        QVERIFY(!PdfExtract::isPdfPath(QStringLiteral("a.docx")));
    }

    // -- detection, like LibreOffice: found, or set by the user ---------------

    void aUserSetPathIsUsed()
    {
        QCOMPARE(PdfExtract::pdftotextPath(m_fake), QFileInfo(m_fake).absoluteFilePath());
        QVERIFY(PdfExtract::available(m_fake));
    }

    void aFolderHoldingItIsFine()
    {
        QCOMPARE(PdfExtract::pdftotextPath(m_dir.path()), QFileInfo(m_fake).absoluteFilePath());
        QVERIFY(PdfExtract::available(m_dir.path() + QLatin1Char('/')));
    }

    void aFileUrlIsFine()
    {
        QVERIFY(PdfExtract::available(QUrl::fromLocalFile(m_fake).toString()));
    }

    void aWrongUserSetPathMeansNotAvailableAndDoesNotFallBack()
    {
        // Even when a real pdftotext is installed: a bad setting is reported, not hidden.
        QVERIFY(!PdfExtract::available(m_dir.filePath(QStringLiteral("no-such-binary"))));
        QVERIFY(PdfExtract::pdftotextPath(m_dir.filePath(QStringLiteral("no-such-binary"))).isEmpty());
        const QString plain = m_dir.filePath(QStringLiteral("not-executable"));
        QFile f(plain);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n");
        f.close();
        QFile::setPermissions(plain, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(!PdfExtract::available(plain));
        QDir().mkdir(m_dir.filePath(QStringLiteral("emptydir")));
        QVERIFY(!PdfExtract::available(m_dir.filePath(QStringLiteral("emptydir"))));
    }

    void withoutASettingItIsAutoDetected()
    {
        // Whatever this machine has, the answer agrees with the detected path.
        QCOMPARE(PdfExtract::available(), !PdfExtract::pdftotextPath().isEmpty());
        QCOMPARE(PdfExtract::available({}), PdfExtract::available());
    }

    void theMissingMessageSaysWhatToDo()
    {
        const QString none = PdfExtract::missingToolMessage();
        QVERIFY(none.contains(QLatin1String("pdftotext")));
        QVERIFY(none.contains(QLatin1String("poppler-utils")));
        const QString bad = PdfExtract::missingToolMessage(QStringLiteral("/opt/nowhere/pdftotext"));
        QVERIFY(bad.contains(QLatin1String("/opt/nowhere/pdftotext")));
        QVERIFY(bad.contains(QLatin1String("No usable pdftotext")));
    }

    // -- extraction ----------------------------------------------------------

    void textIsExtractedAndTidied()
    {
        QString err;
        const QString text = PdfExtract::extract(pdf(QStringLiteral("paper.pdf")), m_fake, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        // Page break becomes a blank line; runs of blank lines are collapsed; layout spacing stays.
        QCOMPARE(text, QStringLiteral("Quarterly Report\n\nNorth   120 135\n\nSecond page text"));
    }

    void pdftotextIsCalledWithLayoutAndUtf8()
    {
        QString err;
        const QString text = PdfExtract::extract(pdf(QStringLiteral("args.pdf")), m_fake, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY2(text.startsWith(QLatin1String("-layout -enc UTF-8 ")), qPrintable(text));
        QVERIFY(text.endsWith(QLatin1String("args.pdf -")));
    }

    void aFailureReportsWhatPdftotextSaid()
    {
        QString err;
        QVERIFY(PdfExtract::extract(pdf(QStringLiteral("locked.pdf")), m_fake, &err).isEmpty());
        QCOMPARE(err, QStringLiteral("Incorrect password"));
    }

    void aPdfWithNoTextIsReportedAsNeedingOcr()
    {
        QString err;
        QVERIFY(PdfExtract::extract(pdf(QStringLiteral("scanned.pdf")), m_fake, &err).isEmpty());
        QVERIFY2(err.contains(QLatin1String("No text found")), qPrintable(err));
        QVERIFY(err.contains(QLatin1String("OCR")));
    }

    void aMissingFileIsReported()
    {
        QString err;
        QVERIFY(PdfExtract::extract(m_dir.filePath(QStringLiteral("gone.pdf")), m_fake, &err).isEmpty());
        QCOMPARE(err, QStringLiteral("File not found."));
    }

    void withoutTheToolTheReasonIsGiven()
    {
        QString err;
        const QString bad = m_dir.filePath(QStringLiteral("no-such-binary"));
        QVERIFY(PdfExtract::extract(pdf(QStringLiteral("paper.pdf")), bad, &err).isEmpty());
        QVERIFY2(err.contains(QLatin1String("No usable pdftotext")), qPrintable(err));
        QVERIFY(err.contains(bad));
    }

    void hugeOutputIsCappedInsteadOfFillingMemory()
    {
        QString err;
        QElapsedTimer t;
        t.start();
        const QString text = PdfExtract::extract(pdf(QStringLiteral("huge.pdf")), m_fake, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY2(t.elapsed() < 20000, qPrintable(QString::number(t.elapsed())));
        QVERIFY(text.endsWith(QLatin1String("[the rest of the PDF is not included]")));
        QVERIFY2(text.size() < 4 * 1024 * 1024 + 200, qPrintable(QString::number(text.size())));
        QVERIFY(text.size() > 3 * 1024 * 1024);
    }

    void outputThatNeverEndsIsCutOffAndTheToolStopped()
    {
        // Without the cap this would run until the one-minute timeout and then fail.
        QString err;
        QElapsedTimer t;
        t.start();
        const QString text = PdfExtract::extract(pdf(QStringLiteral("endless.pdf")), m_fake, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY2(t.elapsed() < 20000, qPrintable(QString::number(t.elapsed())));
        QVERIFY(text.endsWith(QLatin1String("[the rest of the PDF is not included]")));
    }

    // -- with the real pdftotext and a real PDF, when both are there ----------

    void aRealPdfKeepsItsTableLayout()
    {
        if (!PdfExtract::available())
            QSKIP("pdftotext is not installed");
        if (!DocxExport::available({}))
            QSKIP("LibreOffice is needed to make a test PDF");
        QFile html(m_dir.filePath(QStringLiteral("real.html")));
        QVERIFY(html.open(QIODevice::WriteOnly));
        html.write("<html><body><h1>Quarterly Report</h1><p>Revenue grew 12% year over year.</p>"
                   "<table border=\"1\"><tr><th>Region</th><th>Q1</th><th>Q2</th></tr>"
                   "<tr><td>North</td><td>120</td><td>135</td></tr></table>"
                   "<p>caf\xc3\xa9 na\xc3\xafve \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e</p></body></html>");
        html.close();
        QProcess p;
        p.start(DocxExport::sofficePath({}),
                {QStringLiteral("--headless"), QStringLiteral("-env:UserInstallation=file://") + m_dir.filePath(QStringLiteral("lo")),
                 QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), m_dir.path(), html.fileName()});
        QVERIFY(p.waitForFinished(120000));
        const QString real = m_dir.filePath(QStringLiteral("real.pdf"));
        QVERIFY(QFileInfo::exists(real));

        QString err;
        const QString text = PdfExtract::extract(real, {}, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(text.contains(QLatin1String("Quarterly Report")));
        QVERIFY(text.contains(QStringLiteral("café")));
        QVERIFY(text.contains(QStringLiteral("日本語")));
        // -layout keeps a table row on one line.
        bool rowOnOneLine = false;
        for (const QString &line : text.split(QLatin1Char('\n')))
            if (line.contains(QLatin1String("North")) && line.contains(QLatin1String("120")) && line.contains(QLatin1String("135")))
                rowOnOneLine = true;
        QVERIFY2(rowOnOneLine, qPrintable(text));
    }
};

QTEST_GUILESS_MAIN(TestPdfExtract)
#include "test_pdf_extract.moc"
