#include "artifacts/DocxExport.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class TestDocxExport : public QObject
{
    Q_OBJECT
private slots:
    void fileNameFromTitle()
    {
        QCOMPARE(DocxExport::fileNameFromTitle(QStringLiteral("Clover Sales Report — Today")),
                 QString("Clover-Sales-Report-Today.docx"));
        QCOMPARE(DocxExport::fileNameFromTitle(QString()), QString("artifact.docx"));
        QCOMPARE(DocxExport::fileNameFromTitle(QStringLiteral("///")), QString("artifact.docx"));
        QCOMPARE(DocxExport::fileNameFromTitle(QStringLiteral("already.docx")),
                 QString("already.docx"));
    }

    void convertFailsWithoutHtml()
    {
        QTemporaryDir dir;
        const QString err = DocxExport::convertHtmlToDocx(QString(), dir.filePath(QStringLiteral("x.docx")));
        QVERIFY(!err.isEmpty());
    }

    void overridePath()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString bin = dir.filePath(QStringLiteral("soffice"));
        QFile f(bin);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n");
        f.close();
        QVERIFY(QFile::setPermissions(bin, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        QCOMPARE(DocxExport::sofficePath(bin), QFileInfo(bin).absoluteFilePath());
        QVERIFY(DocxExport::available(bin));
        QCOMPARE(DocxExport::sofficePath(dir.path()), QFileInfo(bin).absoluteFilePath());
        QVERIFY(DocxExport::sofficePath(QStringLiteral("/definitely/not/a-soffice-binary")).isEmpty());
        QVERIFY(!DocxExport::available(QStringLiteral("/definitely/not/a-soffice-binary")));
    }

    void convertFailsOnBadOverride()
    {
        QTemporaryDir dir;
        const QString dest = dir.filePath(QStringLiteral("x.docx"));
        const QString html = QStringLiteral("<html><body><p>x</p></body></html>");
        const QString err = DocxExport::convertHtmlToDocx(
            html, dest, QStringLiteral("/definitely/not/a-soffice-binary"));
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("No usable LibreOffice")));
        QVERIFY(!QFile::exists(dest));
    }

    void convertIfLibreOfficePresent()
    {
        if (!DocxExport::available())
            QSKIP("LibreOffice (soffice) is not installed");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dest = dir.filePath(QStringLiteral("hi.docx"));
        const QString html = QStringLiteral(
            "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Hi</title></head>"
            "<body><h1>Hello</h1><p>Word export smoke test.</p></body></html>");
        const QString err = DocxExport::convertHtmlToDocx(html, dest);
        QCOMPARE(err, QString());
        QVERIFY(QFile::exists(dest));
        QFile f(dest);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.read(2), QByteArray("PK"));
    }
};

QTEST_MAIN(TestDocxExport)
#include "test_docx_export.moc"
