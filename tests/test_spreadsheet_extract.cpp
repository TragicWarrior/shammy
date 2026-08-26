#include "artifacts/SpreadsheetExtract.h"
#include "artifacts/DocxExport.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

class TestSpreadsheetExtract : public QObject
{
    Q_OBJECT
private slots:
    void isSpreadsheetPath()
    {
        QVERIFY(SpreadsheetExtract::isSpreadsheetPath(QStringLiteral("a.xlsx")));
        QVERIFY(SpreadsheetExtract::isSpreadsheetPath(QStringLiteral("a.XLS")));
        QVERIFY(SpreadsheetExtract::isSpreadsheetPath(QStringLiteral("a.ods")));
        QVERIFY(!SpreadsheetExtract::isSpreadsheetPath(QStringLiteral("a.csv")));
        QVERIFY(!SpreadsheetExtract::isSpreadsheetPath(QStringLiteral("a.docx")));
    }

    void safeSheetFileName()
    {
        QCOMPARE(SpreadsheetExtract::safeSheetFileName(QStringLiteral("Sales 2024")), QString("Sales-2024"));
        QCOMPARE(SpreadsheetExtract::safeSheetFileName(QStringLiteral("///")), QString("sheet"));
    }

    void sheetsFromMultiHtml()
    {
        const QString html = QStringLiteral(
            "<html><body>"
            "<h1>Overview</h1><A HREF=\"#table0\">Sales</A>"
            "<A NAME=\"table0\"><h1>Sheet 1: <em>Sales</em></h1></A>"
            "<table><tr><td>hour</td><td>amount</td></tr>"
            "<tr><td>8</td><td>12.5</td></tr></table>"
            "<A NAME=\"table1\"><h1>Sheet 2: <em>Notes</em></h1></A>"
            "<table><tr><td>note</td></tr><tr><td>busy lunch</td></tr></table>"
            "</body></html>");
        const auto sheets = SpreadsheetExtract::sheetsFromCalcHtml(html);
        QCOMPARE(sheets.size(), 2);
        QCOMPARE(sheets.at(0).name, QString("Sales"));
        QVERIFY(sheets.at(0).csv.contains(QStringLiteral("hour,amount")));
        QVERIFY(sheets.at(0).csv.contains(QStringLiteral("8,12.5")));
        QCOMPARE(sheets.at(1).name, QString("Notes"));
        QVERIFY(sheets.at(1).csv.contains(QStringLiteral("busy lunch")));
    }

    void sheetsFromOneTableQuotesComma()
    {
        const QString html = QStringLiteral(
            "<html><body><table><tr><td>a</td><td>b,c</td></tr>"
            "<tr><td>1</td><td>2</td></tr></table></body></html>");
        const auto sheets = SpreadsheetExtract::sheetsFromCalcHtml(html);
        QCOMPARE(sheets.size(), 1);
        QCOMPARE(sheets.at(0).name, QString("Sheet1"));
        QVERIFY(sheets.at(0).csv.contains(QStringLiteral("\"b,c\"")));
    }

    void extractIfLibreOfficePresent()
    {
        if (!DocxExport::available())
            QSKIP("LibreOffice (soffice) is not installed");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString csvPath = dir.filePath(QStringLiteral("src.csv"));
        {
            QFile f(csvPath);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("hour,amount\n8,12.5\n9,40\n");
        }
        QProcess conv;
        conv.setWorkingDirectory(dir.path());
        conv.setProcessChannelMode(QProcess::MergedChannels);
        const QString profile = dir.filePath(QStringLiteral("lo-profile"));
        QDir().mkpath(profile);
        conv.start(DocxExport::sofficePath(),
                   {QStringLiteral("--headless"), QStringLiteral("--nologo"),
                    QStringLiteral("--nofirststartwizard"), QStringLiteral("--norestore"),
                    QStringLiteral("--nolockcheck"),
                    QStringLiteral("-env:UserInstallation=%1").arg(QUrl::fromLocalFile(profile).toString()),
                    QStringLiteral("--convert-to"), QStringLiteral("xlsx"),
                    QStringLiteral("--outdir"), dir.path(), csvPath});
        QVERIFY(conv.waitForStarted(10000));
        QVERIFY(conv.waitForFinished(90000));
        QCOMPARE(conv.exitStatus(), QProcess::NormalExit);
        const QString xlsx = dir.filePath(QStringLiteral("src.xlsx"));
        QVERIFY2(QFile::exists(xlsx), qPrintable(QString::fromUtf8(conv.readAll())));
        QString err;
        const auto sheets = SpreadsheetExtract::extract(xlsx, {}, &err);
        QVERIFY2(!sheets.isEmpty(), qPrintable(err));
        QVERIFY(sheets.at(0).csv.contains(QStringLiteral("hour")));
        QVERIFY(sheets.at(0).csv.contains(QStringLiteral("12.5")));
    }
};

QTEST_MAIN(TestSpreadsheetExtract)
#include "test_spreadsheet_extract.moc"
