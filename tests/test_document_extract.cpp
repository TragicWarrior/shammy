#include "artifacts/DocumentExtract.h"
#include "artifacts/DocxExport.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TestDocumentExtract : public QObject
{
    Q_OBJECT
private slots:
    void isDocumentPath()
    {
        QVERIFY(DocumentExtract::isDocumentPath(QStringLiteral("a.docx")));
        QVERIFY(DocumentExtract::isDocumentPath(QStringLiteral("a.DOC")));
        QVERIFY(DocumentExtract::isDocumentPath(QStringLiteral("a.odt")));
        QVERIFY(DocumentExtract::isDocumentPath(QStringLiteral("a.rtf")));
        QVERIFY(!DocumentExtract::isDocumentPath(QStringLiteral("a.xlsx")));
        QVERIFY(!DocumentExtract::isDocumentPath(QStringLiteral("a.txt")));
        QVERIFY(!DocumentExtract::isDocumentPath(QStringLiteral("a.pdf")));
    }

    void textFromWriterHtml()
    {
        const QString html = QStringLiteral(
            "<html><body><h1>Hello</h1><p>Word extract smoke test.</p>"
            "<table><tr><td>hour</td><td>amount</td></tr>"
            "<tr><td>8</td><td>12.5</td></tr></table></body></html>");
        const QString text = DocumentExtract::textFromWriterHtml(html);
        QVERIFY(text.contains(QStringLiteral("Hello")));
        QVERIFY(text.contains(QStringLiteral("Word extract smoke test.")));
        QVERIFY(text.contains(QStringLiteral("hour")));
        QVERIFY(text.contains(QStringLiteral("12.5")));
    }

    void extractFailsWithoutOffice()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("x.docx"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("PK");
        f.close();
        QString err;
        const QString text = DocumentExtract::extract(
            path, QStringLiteral("/definitely/not-a-soffice-binary"), &err);
        QVERIFY(text.isEmpty());
        QVERIFY(err.contains(QStringLiteral("LibreOffice")));
    }

    void extractIfLibreOfficePresent()
    {
        if (!DocxExport::available())
        {
            QSKIP("LibreOffice (soffice) is not installed");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dest = dir.filePath(QStringLiteral("hi.docx"));
        const QString html = QStringLiteral(
            "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Hi</title></head>"
            "<body><h1>Hello</h1><p>Word extract smoke test.</p></body></html>");
        const QString errExport = DocxExport::convertHtmlToDocx(html, dest);
        QCOMPARE(errExport, QString());
        QVERIFY(QFile::exists(dest));
        QString err;
        const QString text = DocumentExtract::extract(dest, {}, &err);
        QVERIFY2(!text.isEmpty(), qPrintable(err));
        QVERIFY(text.contains(QStringLiteral("Hello")));
        QVERIFY(text.contains(QStringLiteral("Word extract smoke test.")));
    }
};

QTEST_MAIN(TestDocumentExtract)
#include "test_document_extract.moc"
