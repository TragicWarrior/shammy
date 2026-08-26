#include "artifacts/Attach.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TestAttach : public QObject
{
    Q_OBJECT
private slots:
    void pastedChipLabel()
    {
        QCOMPARE(Attach::pastedChipLabel(0), QString("[pasted text 0]"));
        QCOMPARE(Attach::pastedChipLabel(999), QString("[pasted text 999]"));
        QCOMPARE(Attach::pastedChipLabel(1000), QString("[pasted text 1k]"));
        QCOMPARE(Attach::pastedChipLabel(3200), QString("[pasted text 3.2k]"));
        QCOMPARE(Attach::pastedChipLabel(1499), QString("[pasted text 1.5k]"));
        QCOMPARE(Attach::pastedChipLabel(32000), QString("[pasted text 32k]"));
        QCOMPARE(Attach::pastedChipLabel(1000000), QString("[pasted text 1M]"));
    }

    void kindForMime()
    {
        QCOMPARE(Attach::kindForMime(QStringLiteral("image/png")), Attach::Kind::Image);
        QCOMPARE(Attach::kindForMime(QStringLiteral("text/plain")), Attach::Kind::Text);
        QCOMPARE(Attach::kindForMime(QStringLiteral("application/json")), Attach::Kind::Text);
        QCOMPARE(Attach::kindForMime(QStringLiteral("application/pdf")), Attach::Kind::Unsupported);
        QCOMPARE(Attach::kindForMime(QStringLiteral("application/zip")), Attach::Kind::Unsupported);
        QCOMPARE(Attach::kindForMime(
                     QStringLiteral(
                         "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"),
                     QStringLiteral("xlsx")),
                 Attach::Kind::Spreadsheet);
        QCOMPARE(Attach::kindForMime(QStringLiteral("application/octet-stream"), QStringLiteral("bin"),
                                    QByteArray("hello world\n")),
                 Attach::Kind::Text);
        QCOMPARE(Attach::kindForMime(QStringLiteral("application/octet-stream"), QStringLiteral("bin"),
                                    QByteArray("a\0b", 3)),
                 Attach::Kind::Unsupported);
    }

    void looksLikeText()
    {
        QVERIFY(Attach::looksLikeText(QByteArray()));
        QVERIFY(Attach::looksLikeText(QByteArray("plain text\n")));
        QVERIFY(!Attach::looksLikeText(QByteArray("a\0b", 3)));
    }

    void kindForPath()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&](const QString &name, const QByteArray &data) -> QString
        {
            const QString p = dir.filePath(name);
            QFile f(p);
            if (!f.open(QIODevice::WriteOnly))
            {
                return {};
            }
            f.write(data);
            f.close();
            return p;
        };
        QCOMPARE(Attach::kindForPath(write(QStringLiteral("notes.txt"), QByteArray("hi"))), Attach::Kind::Text);
        QCOMPARE(Attach::kindForPath(write(QStringLiteral("data.csv"), QByteArray("a,b\n1,2\n"))), Attach::Kind::Text);
        QCOMPARE(Attach::kindForPath(write(QStringLiteral("doc.json"), QByteArray("{}"))), Attach::Kind::Text);
        QCOMPARE(Attach::kindForPath(write(QStringLiteral("sales.xlsx"), QByteArray("PK"))), Attach::Kind::Spreadsheet);
        QCOMPARE(Attach::kindForPath(write(QStringLiteral("book.ods"), QByteArray("PK"))), Attach::Kind::Spreadsheet);
        QCOMPARE(Attach::kindForPath(write(QStringLiteral("file.pdf"), QByteArray("%PDF-1.4"))), Attach::Kind::Unsupported);
        QCOMPARE(Attach::kindForPath(write(QStringLiteral("pic.png"), QByteArray("\x89PNG"))), Attach::Kind::Image);
        QCOMPARE(Attach::kindForPath(write(QStringLiteral("noext"), QByteArray("just words"))), Attach::Kind::Text);
        QCOMPARE(Attach::kindForPath(write(QStringLiteral("blob.bin"), QByteArray("\x00\x01\x02\xff", 4))),
                 Attach::Kind::Unsupported);
    }

    void pasteThreshold()
    {
        QCOMPARE(Attach::kPasteChipMin, 1000);
    }
};

QTEST_MAIN(TestAttach)
#include "test_attach.moc"
