#include "FakeTools.h"
#include "Util.h"
#include "artifacts/DocxExport.h"
#include "artifacts/PdfExtract.h"
#include "controllers/ProjectController.h"
#include "persist/Store.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

// Project files: what may be added (B5), what happens to them on disk when they
// are removed (B6), and how much of them is sent to the model (B7).

namespace
{

template <class Pred>
bool waitFor(Pred pred, int ms = 15000)
{
    QElapsedTimer t;
    t.start();
    while (!pred())
    {
        if (t.elapsed() > ms)
            return false;
        QTest::qWait(5);
    }
    return true;
}




} // namespace

class TestProjectFiles : public QObject
{
    Q_OBJECT
    QTemporaryDir m_root;
    QString m_fakeOffice;
    QString m_fakePdf;

    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<Store> store;
    std::unique_ptr<ProjectController> pc;
    QString pid;

    QString write(const QString &name, const QByteArray &data)
    {
        const QString path = dir->filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
            qFatal("cannot write %s", qPrintable(path));
        return path;
    }
    static void useOffice(const QString &path)
    {
        QSettings s;
        s.setValue(QStringLiteral("officeBinaryPath"), path);
        s.sync();
    }
    static void usePdftotext(const QString &path)
    {
        QSettings s;
        s.setValue(QStringLiteral("pdftotextBinaryPath"), path);
        s.sync();
    }
    QStringList fileNames() const
    {
        QStringList names;
        for (const ProjectFile &f : store->projectFiles(pid))
            names << f.filename;
        return names;
    }
    QString storedPath(const QString &name) const { return ProjectController::projectsRoot() + QLatin1Char('/') + pid + QLatin1Char('/') + name; }
    static QByteArray readAll(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    bool convertDone() const { return !pc->filesBusy(); }

private slots:
    void initTestCase()
    {
        if (!QFile::exists(QStringLiteral("/bin/sh")))
            QSKIP("needs /bin/sh for the fake soffice");
        QVERIFY(m_root.isValid());
        // Keep app data and QSettings away from the real profile.
        qputenv("XDG_DATA_HOME", m_root.filePath(QStringLiteral("data")).toUtf8());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_root.filePath(QStringLiteral("conf")));
        QCoreApplication::setOrganizationName(QStringLiteral("shammy-test"));
        QCoreApplication::setApplicationName(QStringLiteral("shammy-files-test"));
#ifdef Q_OS_MACOS
        // No XDG_DATA_HOME there: Qt's test mode moves app data to a "qttest" folder.
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(ProjectController::projectsRoot().contains(QStringLiteral("qttest")));
#else
        QVERIFY(ProjectController::projectsRoot().startsWith(m_root.path()));
#endif
        m_fakeOffice = m_root.filePath(QStringLiteral("fake-soffice"));
        QVERIFY(FakeTools::install(m_fakeOffice, FakeTools::soffice));
        m_fakePdf = m_root.filePath(QStringLiteral("fake-pdftotext"));
        QVERIFY(FakeTools::install(m_fakePdf, FakeTools::pdftotext));
    }

    void init()
    {
        useOffice(m_fakeOffice);
        usePdftotext(m_fakePdf);
        dir = std::make_unique<QTemporaryDir>();
        QVERIFY(dir->isValid());
        store = std::make_unique<Store>();
        QVERIFY(store->open(dir->filePath(QStringLiteral("t.db"))));
        pc = std::make_unique<ProjectController>(store.get());
        pc->createProject(QStringLiteral("Test project"));
        pid = pc->currentProjectId();
        QVERIFY(!pid.isEmpty());
    }

    void cleanup()
    {
        // Let a conversion that is still running finish before tearing down.
        waitFor([this]() { return !pc->filesBusy(); });
        pc.reset();
        store.reset();
        dir.reset();
    }

    // -- B5: what may be added --------------------------------------------------

    void textFilesAreCopiedAndListed()
    {
        pc->addFile(write(QStringLiteral("notes.md"), "# Notes\nhello\n"));
        QCOMPARE(fileNames(), QStringList{QStringLiteral("notes.md")});
        QCOMPARE(readAll(storedPath(QStringLiteral("notes.md"))), QByteArray("# Notes\nhello\n"));
        QVERIFY(pc->filesError().isEmpty());
        QVERIFY(!pc->filesBusy());
        QVERIFY(pc->projectContextFor(pid, 262144, nullptr, nullptr).contains(QLatin1String("hello")));
    }

    void addingATextFileAgainReplacesIt()
    {
        pc->addFile(write(QStringLiteral("notes.md"), "first"));
        pc->addFile(write(QStringLiteral("notes.md"), "second version"));
        QCOMPARE(fileNames(), QStringList{QStringLiteral("notes.md")});
        QCOMPARE(readAll(storedPath(QStringLiteral("notes.md"))), QByteArray("second version"));
    }

    void addingTheStoredCopyItselfDoesNotDestroyIt()
    {
        pc->addFile(write(QStringLiteral("notes.md"), "keep me"));
        pc->addFile(storedPath(QStringLiteral("notes.md"))); // the copy Shammy already holds
        QCOMPARE(readAll(storedPath(QStringLiteral("notes.md"))), QByteArray("keep me"));
        QCOMPARE(fileNames().size(), 1);
    }

    void imagesAreRefused()
    {
        pc->addFile(write(QStringLiteral("photo.png"), QByteArray("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16)));
        QVERIFY2(pc->filesError().contains(QLatin1String("images can't be added")), qPrintable(pc->filesError()));
        QVERIFY(pc->filesError().contains(QLatin1String("photo.png")));
        QVERIFY(fileNames().isEmpty());
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("photo.png"))));
    }

    void archivesAndOtherBinariesAreRefused()
    {
        pc->addFile(write(QStringLiteral("bundle.zip"), QByteArray("PK\x03\x04\x14\0\0\0", 8)));
        pc->addFile(write(QStringLiteral("blob.dat"), QByteArray("\0\1\2\3\xff\xfe\0\0", 8)));
        const QStringList lines = pc->filesError().split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 2);
        for (const QString &line : lines)
            QVERIFY2(line.contains(QLatin1String("isn't text")), qPrintable(line));
        QVERIFY(fileNames().isEmpty());
    }

    void missingFilesAndFoldersAreReported()
    {
        pc->addFile(dir->filePath(QStringLiteral("nope.txt")));
        QDir(dir->path()).mkdir(QStringLiteral("subdir"));
        pc->addFile(dir->filePath(QStringLiteral("subdir")));
        QVERIFY2(pc->filesError().contains(QLatin1String("file not found")), qPrintable(pc->filesError()));
        QVERIFY2(pc->filesError().contains(QLatin1String("that's a folder")), qPrintable(pc->filesError()));
        QVERIFY(fileNames().isEmpty());
    }

    void wordDocumentsAreConvertedToTextInTheBackground()
    {
        // The conversion takes a second here; the caller must not wait for it.
        QElapsedTimer t;
        t.start();
        pc->addFile(write(QStringLiteral("slow-report.docx"), "not really a docx"));
        QVERIFY2(t.elapsed() < 400, qPrintable(QString::number(t.elapsed())));
        QVERIFY(pc->filesBusy());
        QVERIFY(fileNames().isEmpty());

        QVERIFY(waitFor([this]() { return convertDone(); }));
        QVERIFY2(pc->filesError().isEmpty(), qPrintable(pc->filesError()));
        // Stored as the text the model will read, not as the Word file.
        QCOMPARE(fileNames(), QStringList{QStringLiteral("slow-report.docx.txt")});
        const QByteArray text = readAll(storedPath(QStringLiteral("slow-report.docx.txt")));
        QVERIFY(text.contains("Hello from the document"));
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("slow-report.docx"))));
        QVERIFY(pc->projectContextFor(pid, 262144, nullptr, nullptr).contains(QLatin1String("Hello from the document")));
    }

    // -- PDFs: supported only while pdftotext is found ---------------------------

    void pdfsAreConvertedToText()
    {
        pc->addFile(write(QStringLiteral("paper.pdf"), "%PDF-1.4\n"));
        QVERIFY(waitFor([this]() { return convertDone(); }));
        QVERIFY2(pc->filesError().isEmpty(), qPrintable(pc->filesError()));
        QCOMPARE(fileNames(), QStringList{QStringLiteral("paper.pdf.txt")});
        QCOMPARE(readAll(storedPath(QStringLiteral("paper.pdf.txt"))),
                 QByteArray("Quarterly Report\n\nNorth   120 135\n\nSecond page text"));
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("paper.pdf"))));
        QVERIFY(pc->projectContextFor(pid, 262144, nullptr, nullptr).contains(QLatin1String("Second page text")));
    }

    void withoutPdftotextAPdfIsRefusedAtOnceWithAReason()
    {
        const QString bad = m_root.filePath(QStringLiteral("no-such-pdftotext"));
        usePdftotext(bad);
        QSignalSpy busy(pc.get(), &ProjectController::filesBusyChanged);
        pc->addFile(write(QStringLiteral("paper.pdf"), "%PDF-1.4\n"));
        // Refused on the spot: no background job was started.
        QCOMPARE(busy.count(), 0);
        QVERIFY(!pc->filesBusy());
        QVERIFY2(pc->filesError().contains(QLatin1String("No usable pdftotext")), qPrintable(pc->filesError()));
        QVERIFY(pc->filesError().contains(QLatin1String("paper.pdf")));
        QVERIFY(pc->filesError().contains(bad));
        QVERIFY(fileNames().isEmpty());
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("paper.pdf"))));
    }

    void withoutPdftotextSetOrFoundThePdfIsRefusedWithInstallAdvice()
    {
        // No setting: it falls back to auto-detection. Only assert the refusal when this machine has none.
        usePdftotext({});
        if (PdfExtract::available())
            QSKIP("pdftotext is installed here, so a PDF would be accepted");
        pc->addFile(write(QStringLiteral("paper.pdf"), "%PDF-1.4\n"));
        QVERIFY(!pc->filesBusy());
        QVERIFY2(pc->filesError().contains(QLatin1String("poppler-utils")), qPrintable(pc->filesError()));
        QVERIFY(fileNames().isEmpty());
    }

    void otherFilesAreUnaffectedByAMissingPdftotext()
    {
        usePdftotext(m_root.filePath(QStringLiteral("no-such-pdftotext")));
        pc->addFile(write(QStringLiteral("notes.md"), "still fine"));
        pc->addFile(write(QStringLiteral("report.docx"), "x"));
        QVERIFY(waitFor([this]() { return convertDone(); }));
        QVERIFY2(pc->filesError().isEmpty(), qPrintable(pc->filesError()));
        QCOMPARE(fileNames(), (QStringList{QStringLiteral("notes.md"), QStringLiteral("report.docx.txt")}));
    }

    void aPdfWithNoTextIsReportedAsNeedingOcr()
    {
        pc->addFile(write(QStringLiteral("scanned.pdf"), "%PDF-1.4\n"));
        QVERIFY(waitFor([this]() { return convertDone(); }));
        QVERIFY2(pc->filesError().contains(QLatin1String("OCR")), qPrintable(pc->filesError()));
        QVERIFY(fileNames().isEmpty());
    }

    void aPdfThatCannotBeReadSaysWhy()
    {
        pc->addFile(write(QStringLiteral("locked.pdf"), "%PDF-1.4\n"));
        QVERIFY(waitFor([this]() { return convertDone(); }));
        QVERIFY2(pc->filesError().contains(QLatin1String("Incorrect password")), qPrintable(pc->filesError()));
        QVERIFY(pc->filesError().contains(QLatin1String("locked.pdf")));
        QVERIFY(fileNames().isEmpty());
    }

    void spreadsheetsBecomeOneCsvPerSheet()
    {
        pc->addFile(write(QStringLiteral("sales.xlsx"), "not really an xlsx"));
        QVERIFY(waitFor([this]() { return convertDone(); }));
        QVERIFY2(pc->filesError().isEmpty(), qPrintable(pc->filesError()));
        // Both sheets are called "Data": the second gets a distinct name instead of overwriting the first.
        QCOMPARE(fileNames(), (QStringList{QStringLiteral("sales-Data-2.csv"), QStringLiteral("sales-Data.csv")}));
        QCOMPARE(readAll(storedPath(QStringLiteral("sales-Data.csv"))), QByteArray("name,qty\napple,3"));
        QCOMPARE(readAll(storedPath(QStringLiteral("sales-Data-2.csv"))), QByteArray("x"));
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("sales.xlsx"))));
    }

    void aFailedConversionIsReportedAndNothingIsStored()
    {
        // Before, the unconverted Word/Excel bytes were stored and pasted into the prompt.
        pc->addFile(write(QStringLiteral("broken.docx"), "not really a docx"));
        pc->addFile(write(QStringLiteral("broken.xlsx"), "not really an xlsx"));
        QVERIFY(waitFor([this]() { return convertDone(); }));
        QVERIFY2(pc->filesError().contains(QLatin1String("conversion exploded")), qPrintable(pc->filesError()));
        QCOMPARE(pc->filesError().split(QLatin1Char('\n')).size(), 2);
        QVERIFY(fileNames().isEmpty());
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("broken.docx"))));
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("broken.xlsx"))));
    }

    void withoutAnOfficeSuiteConvertibleFilesAreRefusedWithAReason()
    {
        useOffice(m_root.filePath(QStringLiteral("no-such-soffice")));
        pc->addFile(write(QStringLiteral("report.docx"), "x"));
        QVERIFY(waitFor([this]() { return convertDone(); }));
        QVERIFY2(pc->filesError().contains(QLatin1String("LibreOffice")) || pc->filesError().contains(QLatin1String("OpenOffice")),
                 qPrintable(pc->filesError()));
        QVERIFY(fileNames().isEmpty());
    }

    void aProjectDeletedDuringConversionGetsNothing()
    {
        pc->addFile(write(QStringLiteral("slow.docx"), "x"));
        QVERIFY(pc->filesBusy());
        pc->deleteProject(pid);
        QVERIFY(waitFor([this]() { return convertDone(); }));
        QVERIFY(store->projectFiles(pid).isEmpty());
        QVERIFY(!QFileInfo::exists(ProjectController::projectsRoot() + QLatin1Char('/') + pid));
    }

    void errorsAccumulateForABatchAndClear()
    {
        pc->addFile(write(QStringLiteral("a.png"), QByteArray("\x89PNG\r\n\x1a\n", 8)));
        pc->addFile(write(QStringLiteral("b.png"), QByteArray("\x89PNG\r\n\x1a\n", 8)));
        QCOMPARE(pc->filesError().split(QLatin1Char('\n')).size(), 2);
        QSignalSpy spy(pc.get(), &ProjectController::filesErrorChanged);
        pc->clearFilesError();
        QVERIFY(pc->filesError().isEmpty());
        QCOMPARE(spy.count(), 1);
        pc->clearFilesError(); // nothing to clear: no signal
        QCOMPARE(spy.count(), 1);

        pc->addFile(write(QStringLiteral("c.png"), QByteArray("\x89PNG\r\n\x1a\n", 8)));
        QVERIFY(!pc->filesError().isEmpty());
        pc->createProject(QStringLiteral("Another")); // moving to another project drops the old message
        QVERIFY(pc->filesError().isEmpty());
    }

    void legacyBinaryFilesAreNamedNotPasted()
    {
        // A Word file stored raw by an earlier version, next to a good text file.
        pc->addFile(write(QStringLiteral("good.md"), "useful notes"));
        const QByteArray raw("PK\x03\x04\0\0\0\0garbage-that-would-poison-the-prompt\xff\xfe", 46);
        const QString legacy = storedPath(QStringLiteral("old.docx"));
        QFile f(legacy);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(raw);
        f.close();
        ProjectFile row;
        row.id = newId();
        row.projectId = pid;
        row.filename = QStringLiteral("old.docx");
        row.path = legacy;
        row.size = raw.size();
        store->upsertProjectFile(row);

        int included = 0, truncated = 0, omitted = 0;
        const QString ctx = pc->projectContextFor(pid, 262144, &included, &truncated, &omitted);
        QVERIFY(ctx.contains(QLatin1String("useful notes")));
        QVERIFY(ctx.contains(QLatin1String("## old.docx")));
        QVERIFY(ctx.contains(QLatin1String("not included: this file is not text")));
        QVERIFY(!ctx.contains(QLatin1String("garbage-that-would-poison")));
        QCOMPARE(included, 1);
    }

    // -- B6: disk lifecycle -----------------------------------------------------

    void removingAFileDeletesItFromDisk()
    {
        pc->addFile(write(QStringLiteral("one.md"), "1"));
        pc->addFile(write(QStringLiteral("two.md"), "2"));
        QVERIFY(QFileInfo::exists(storedPath(QStringLiteral("one.md"))));
        QString oneId;
        for (const ProjectFile &f : store->projectFiles(pid))
            if (f.filename == QLatin1String("one.md"))
                oneId = f.id;
        pc->removeFile(oneId);
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("one.md"))));
        QVERIFY(QFileInfo::exists(storedPath(QStringLiteral("two.md"))));
        QCOMPARE(fileNames(), QStringList{QStringLiteral("two.md")});
    }

    void removingAFileNeverDeletesOutsideTheProjectsFolder()
    {
        // The stored path comes from the database, so it is not trusted.
        const QString precious = write(QStringLiteral("precious.txt"), "do not delete");
        ProjectFile row;
        row.id = newId();
        row.projectId = pid;
        row.filename = QStringLiteral("precious.txt");
        row.path = precious;
        row.size = 13;
        store->upsertProjectFile(row);
        pc->removeFile(row.id);
        QVERIFY(store->projectFiles(pid).isEmpty()); // the entry is gone...
        QVERIFY(QFileInfo::exists(precious));       // ...the file it pointed at is not

        // A symlink inside the folder that points out of it.
        QDir().mkpath(ProjectController::projectsRoot() + QLatin1Char('/') + pid);
        const QString link = storedPath(QStringLiteral("link.txt"));
        QVERIFY(QFile::link(precious, link));
        ProjectFile viaLink = row;
        viaLink.id = newId();
        viaLink.filename = QStringLiteral("link.txt");
        viaLink.path = link;
        store->upsertProjectFile(viaLink);
        pc->removeFile(viaLink.id);
        QVERIFY(QFileInfo::exists(precious));
    }

    void removingAFileWithAnUnknownIdIsHarmless()
    {
        pc->addFile(write(QStringLiteral("keep.md"), "k"));
        pc->removeFile(QStringLiteral("no-such-id"));
        QCOMPARE(fileNames(), QStringList{QStringLiteral("keep.md")});
        QVERIFY(QFileInfo::exists(storedPath(QStringLiteral("keep.md"))));
    }

    void deletingAProjectDeletesItsFolderAndOnlyItsFolder()
    {
        pc->addFile(write(QStringLiteral("a.md"), "a"));
        const QString first = pid;
        pc->createProject(QStringLiteral("Second"));
        pid = pc->currentProjectId();
        pc->addFile(write(QStringLiteral("b.md"), "b"));
        const QString second = pid;
        const QString firstDir = ProjectController::projectsRoot() + QLatin1Char('/') + first;
        const QString secondDir = ProjectController::projectsRoot() + QLatin1Char('/') + second;
        QVERIFY(QFileInfo::exists(firstDir + QStringLiteral("/a.md")));

        pc->deleteProject(first);
        QVERIFY(!QFileInfo::exists(firstDir));
        QVERIFY(store->projectFiles(first).isEmpty());
        QVERIFY(store->project(first).id.isEmpty());
        QVERIFY(QFileInfo::exists(secondDir + QStringLiteral("/b.md")));
        QCOMPARE(store->projectFiles(second).size(), 1);
    }

    void deletingAProjectWithAnUnsafeIdTouchesNoFolder()
    {
        const QString victim = ProjectController::projectsRoot() + QStringLiteral("/../victim");
        QVERIFY(QDir().mkpath(victim));
        QFile f(victim + QStringLiteral("/keep.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("keep");
        f.close();
        Project p;
        p.id = QStringLiteral("../victim");
        p.name = QStringLiteral("evil");
        p.createdAt = p.updatedAt = nowMs();
        store->upsertProject(p);
        pc->deleteProject(p.id);
        QVERIFY(store->project(p.id).id.isEmpty());
        QVERIFY(QFileInfo::exists(victim + QStringLiteral("/keep.txt")));
        QDir(victim).removeRecursively();
    }

    void anIdWithDotDotCannotReachAnotherProjectsFolder()
    {
        // Stays inside the projects folder, so only the "plain name" check stops it.
        pc->addFile(write(QStringLiteral("mine.md"), "mine"));
        const QString root = ProjectController::projectsRoot();
        QVERIFY(QDir().mkpath(root + QStringLiteral("/zzz")));
        Project p;
        p.id = QStringLiteral("zzz/../") + pid;
        p.name = QStringLiteral("sneaky");
        p.createdAt = p.updatedAt = nowMs();
        store->upsertProject(p);
        pc->deleteProject(p.id);
        QVERIFY(QFileInfo::exists(storedPath(QStringLiteral("mine.md"))));
        QDir(root + QStringLiteral("/zzz")).removeRecursively();
    }

    // -- B7: the budget ---------------------------------------------------------

    void budgetScalesWithTheContextWindow()
    {
        // 40% of the window at ~4 bytes a token, never more than 256 KB.
        QCOMPARE(ProjectController::preloadBudgetBytes(16384), qint64(26214));
        QCOMPARE(ProjectController::preloadBudgetBytes(32768), qint64(52428));
        QCOMPARE(ProjectController::preloadBudgetBytes(131072), qint64(209715));
        QCOMPARE(ProjectController::preloadBudgetBytes(262144), qint64(256 * 1024));
        QCOMPARE(ProjectController::preloadBudgetBytes(1048576), qint64(256 * 1024));
        // A tiny or nonsense window still leaves a small floor.
        QCOMPARE(ProjectController::preloadBudgetBytes(0), qint64(2048));
        QCOMPARE(ProjectController::preloadBudgetBytes(-5), qint64(2048));
        QCOMPARE(ProjectController::preloadBudgetBytes(1024), qint64(2048));
        // One file may take at most half the budget, but never less than 32 KB (or the whole budget if smaller).
        QCOMPARE(ProjectController::perFileLimitBytes(26214), qint64(26214));
        QCOMPARE(ProjectController::perFileLimitBytes(209715), qint64(104857));
        QCOMPARE(ProjectController::perFileLimitBytes(256 * 1024), qint64(128 * 1024));
        // The budget always fits inside the window it was made for.
        for (int tokens : {512, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144})
            QVERIFY2(ProjectController::preloadBudgetBytes(tokens) <= qint64(tokens) * 4 * 40 / 100 || tokens < 5120,
                     qPrintable(QString::number(tokens)));
    }

    void projectFilesAreCutToFitTheModelsContext()
    {
        Project p = store->project(pid);
        p.instructions = QString(1024, QLatin1Char('i'));
        store->upsertProject(p);
        for (const char *name : {"a.txt", "b.txt", "c.txt"})
            pc->addFileFromContent(QString::fromLatin1(name), QByteArray(20000, name[0]));

        // A 16K window: 26214 B of budget, 1024 of it taken by the instructions.
        int included = 0, truncated = 0, omitted = 0;
        const QString small = pc->projectContextFor(pid, 16384, &included, &truncated, &omitted);
        QCOMPARE(included, 2);   // a.txt whole, b.txt cut to what is left
        QCOMPARE(truncated, 1);
        QCOMPARE(omitted, 1);    // c.txt does not fit at all
        QVERIFY(small.contains(QLatin1String("## a.txt")));
        QVERIFY(small.contains(QLatin1String("## b.txt")));
        QVERIFY(!small.contains(QLatin1String("## c.txt")));
        QVERIFY(small.contains(QLatin1String("over the size budget: c.txt")));
        QVERIFY2(small.toUtf8().size() < 26214, qPrintable(QString::number(small.toUtf8().size())));

        // A big window takes everything.
        const QString big = pc->projectContextFor(pid, 262144, &included, &truncated, &omitted);
        QCOMPARE(included, 3);
        QCOMPARE(truncated, 0);
        QCOMPARE(omitted, 0);
        QVERIFY(!big.contains(QLatin1String("over the size budget")));
    }

    void oneHugeFileCannotTakeTheWholeBudget()
    {
        pc->addFileFromContent(QStringLiteral("huge.txt"), QByteArray(150 * 1024, 'h'));
        pc->addFileFromContent(QStringLiteral("small1.txt"), QByteArray(1000, 's'));
        pc->addFileFromContent(QStringLiteral("small2.txt"), QByteArray(1000, 't'));
        int included = 0, truncated = 0, omitted = 0;
        // A 128K window: budget 209715, so one file may take 104857.
        pc->projectContextFor(pid, 131072, &included, &truncated, &omitted);
        QCOMPARE(included, 3);
        QCOMPARE(truncated, 1);
        QCOMPARE(omitted, 0);
    }

    void aFileIsNeverCutInTheMiddleOfACharacter()
    {
        // 4-byte characters; the budget (26214) is not a multiple of 4, so a byte cut lands mid-character.
        QByteArray text;
        for (int i = 0; i < 20000; ++i)
            text += "\xF0\x9F\x98\x80";
        pc->addFileFromContent(QStringLiteral("emoji.txt"), text);
        const QString ctx = pc->projectContextFor(pid, 16384, nullptr, nullptr);
        QVERIFY(ctx.contains(QChar::fromUcs4(0x1F600)));
        QVERIFY(!ctx.contains(QChar(0xFFFD)));
    }

    void theGaugeFollowsTheSelectedModelsContext()
    {
        pc->addFileFromContent(QStringLiteral("big.txt"), QByteArray(40 * 1024, 'x'));
        pc->setContextTokens(16384);
        QCOMPARE(pc->preloadBudgetBytes(), qint64(26214));
        QVERIFY(pc->fileOverCapacity());
        QVERIFY(pc->fileUsagePercent() > 100);
        QVERIFY2(pc->fileUsageLabel().contains(QLatin1String("over budget")), qPrintable(pc->fileUsageLabel()));
        QVERIFY2(pc->fileBudgetNote().contains(QLatin1String("16K")), qPrintable(pc->fileBudgetNote()));

        QSignalSpy spy(pc.get(), &ProjectController::filesChanged);
        pc->setContextTokens(262144);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!pc->fileOverCapacity());
        QVERIFY(pc->fileUsagePercent() < 100);
        QVERIFY2(!pc->fileUsageLabel().contains(QLatin1String("over budget")), qPrintable(pc->fileUsageLabel()));
        QVERIFY2(pc->fileBudgetNote().contains(QLatin1String("256K")), qPrintable(pc->fileBudgetNote()));
        pc->setContextTokens(262144); // unchanged: no signal
        pc->setContextTokens(0);      // nonsense: ignored
        QCOMPARE(spy.count(), 1);
        QCOMPARE(pc->contextTokens(), 262144);
    }

    void oneOversizedFileTripsTheWarningEvenWhenTheTotalFits()
    {
        pc->addFileFromContent(QStringLiteral("long.txt"), QByteArray(150 * 1024, 'x'));
        pc->setContextTokens(131072); // budget 209715 > 150 KB, but one file may only take 104857
        QVERIFY(pc->fileOverCapacity());
    }

    void instructionsCountAgainstTheBudget()
    {
        Project p = store->project(pid);
        p.instructions = QString(30000, QLatin1Char('i'));
        store->upsertProject(p);
        pc->addFileFromContent(QStringLiteral("f.txt"), QByteArray(1000, 'f'));
        pc->setContextTokens(16384); // budget 26214: the instructions alone are over it
        QVERIFY(pc->fileOverCapacity());
        int included = 0, omitted = 0, truncated = 0;
        pc->projectContextFor(pid, 16384, &included, &truncated, &omitted);
        QCOMPARE(included, 0);
        QCOMPARE(omitted, 1);
    }

    // -- with a real pdftotext and LibreOffice (to make the PDF), when there are ----

    void aRealPdfBecomesProjectContextThroughTheRealTool()
    {
        if (!PdfExtract::available())
            QSKIP("pdftotext is not installed");
        if (!DocxExport::available({}))
            QSKIP("LibreOffice is needed to make a test PDF");
        usePdftotext({}); // auto-detect the real one
        const QString html = write(QStringLiteral("brief.html"),
                                   "<html><body><h1>Launch plan</h1><p>Ship the beta in March.</p>"
                                   "<table border=\"1\"><tr><td>Owner</td><td>Dana</td></tr></table></body></html>");
        QProcess p;
        p.setWorkingDirectory(dir->path());
        p.start(DocxExport::sofficePath({}),
                {QStringLiteral("--headless"), QStringLiteral("-env:UserInstallation=file://") + dir->filePath(QStringLiteral("lo")),
                 QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), dir->path(), html});
        QVERIFY(p.waitForFinished(120000));
        const QString real = dir->filePath(QStringLiteral("brief.pdf"));
        QVERIFY(QFileInfo::exists(real));

        pc->addFile(real);
        QVERIFY(waitFor([this]() { return convertDone(); }, 60000));
        QVERIFY2(pc->filesError().isEmpty(), qPrintable(pc->filesError()));
        QCOMPARE(fileNames(), QStringList{QStringLiteral("brief.pdf.txt")});
        const QByteArray text = readAll(storedPath(QStringLiteral("brief.pdf.txt")));
        QVERIFY2(text.contains("Launch plan") && text.contains("Ship the beta in March.") && text.contains("Dana"), text.constData());
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("brief.pdf"))));
        QVERIFY(pc->projectContextFor(pid, 262144, nullptr, nullptr).contains(QLatin1String("Ship the beta in March.")));
    }

    // -- with a real LibreOffice, when there is one ------------------------------

    void realLibreOfficeConvertsWordAndSpreadsheetFiles()
    {
        if (!DocxExport::available({}))
            QSKIP("LibreOffice is not installed");
        useOffice({}); // auto-detect the real one
        // Make real .docx and .xlsx files with LibreOffice itself.
        const QString html = write(QStringLiteral("src.html"), "<html><body><p>Quarterly plan for the real docx</p></body></html>");
        const QString docx = dir->filePath(QStringLiteral("plan.docx"));
        QVERIFY2(DocxExport::convertHtmlToDocx(QString::fromUtf8(readAll(html)), docx, {}).isEmpty(), "docx creation failed");
        const QString csv = write(QStringLiteral("table.csv"), "item,count\nwidgets,12\n");
        QProcess p;
        p.setWorkingDirectory(dir->path());
        p.start(DocxExport::sofficePath({}),
                {QStringLiteral("--headless"), QStringLiteral("-env:UserInstallation=file://") + dir->filePath(QStringLiteral("lo")),
                 QStringLiteral("--convert-to"), QStringLiteral("xlsx"), QStringLiteral("--outdir"), dir->path(), csv});
        QVERIFY(p.waitForFinished(120000));
        const QString xlsx = dir->filePath(QStringLiteral("table.xlsx"));
        QVERIFY(QFileInfo::exists(xlsx));

        pc->addFile(docx);
        pc->addFile(xlsx);
        QVERIFY(waitFor([this]() { return convertDone(); }, 120000));
        QVERIFY2(pc->filesError().isEmpty(), qPrintable(pc->filesError()));
        const QStringList names = fileNames();
        QVERIFY2(names.contains(QStringLiteral("plan.docx.txt")), qPrintable(names.join(QLatin1Char(','))));
        QVERIFY(readAll(storedPath(QStringLiteral("plan.docx.txt"))).contains("Quarterly plan for the real docx"));
        QStringList csvs;
        for (const QString &n : names)
            if (n.endsWith(QLatin1String(".csv")))
                csvs << n;
        QCOMPARE(csvs.size(), 1);
        const QByteArray sheet = readAll(storedPath(csvs.first()));
        QVERIFY2(sheet.contains("widgets,12"), sheet.constData());
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("plan.docx"))));
        QVERIFY(!QFileInfo::exists(storedPath(QStringLiteral("table.xlsx"))));
    }
};

QTEST_GUILESS_MAIN(TestProjectFiles)
#include "test_project_files.moc"
