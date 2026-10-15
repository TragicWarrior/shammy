#include "shell/ShellCommand.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_UNIX
#include <signal.h>
#endif

class TestShellCommand : public QObject
{
    Q_OBJECT

    struct Outcome
    {
        bool done = false;
        QString text;
        QString error;
    };

    static bool waitDone(const std::shared_ptr<Outcome> &o, int ms)
    {
        QElapsedTimer t;
        t.start();
        while (!o->done && t.elapsed() < ms)
            QTest::qWait(20);
        return o->done;
    }
    static std::shared_ptr<Outcome> start(ShellCommand &sh, const QString &cmd, ShellCommand::Options opts = {})
    {
        auto out = std::make_shared<Outcome>();
        sh.run(cmd, opts, [out](const QString &t, const QString &e)
        {
            out->done = true;
            out->text = t;
            out->error = e;
        });
        return out;
    }

private slots:
    void initTestCase()
    {
#ifndef Q_OS_UNIX
        QSKIP("commands are tested with a POSIX shell");
#endif
    }

    void outputAndExitCode()
    {
        ShellCommand sh;
        auto o = start(sh, QStringLiteral("echo hello; echo oops >&2; exit 3"));
        QVERIFY(waitDone(o, 10000));
        QCOMPARE(o->error, QString());
        QCOMPARE(o->text, QStringLiteral("hello\noops\n[exit code 3]"));
        QCOMPARE(sh.runningCount(), 0);
        o = start(sh, QStringLiteral("true"));
        QVERIFY(waitDone(o, 10000));
        QCOMPARE(o->text, QStringLiteral("(no output)\n[exit code 0]"));
    }

    void runsInTheGivenFolderOrHome()
    {
        QTemporaryDir dir;
        ShellCommand sh;
        ShellCommand::Options opts;
        opts.workingDirectory = dir.path();
        auto o = start(sh, QStringLiteral("pwd -P"), opts);
        QVERIFY(waitDone(o, 10000));
        QCOMPARE(o->text, QFileInfo(dir.path()).canonicalFilePath() + QStringLiteral("\n[exit code 0]"));
        o = start(sh, QStringLiteral("pwd -P"));
        QVERIFY(waitDone(o, 10000));
        QCOMPARE(o->text, QFileInfo(QDir::homePath()).canonicalFilePath() + QStringLiteral("\n[exit code 0]"));
        opts.workingDirectory = dir.filePath(QStringLiteral("nope"));
        o = start(sh, QStringLiteral("pwd"), opts);
        QVERIFY(waitDone(o, 1000));
        QVERIFY(o->error.startsWith(QLatin1String("There is no folder")));
    }

    void nothingCanWaitForInput()
    {
        ShellCommand sh;
        QElapsedTimer t;
        t.start();
        auto o = start(sh, QStringLiteral("read x; echo \"got:$x\""));
        QVERIFY(waitDone(o, 5000));
        QVERIFY(t.elapsed() < 4000);
        QVERIFY2(o->text.startsWith(QLatin1String("got:\n")), qPrintable(o->text));
    }

    void longOutputKeepsItsStartAndEnd()
    {
        ShellCommand sh;
        ShellCommand::Options opts;
        opts.maxOutputBytes = 4096;
        auto o = start(sh, QStringLiteral("echo FIRST; i=0; while [ $i -lt 5000 ]; do echo line$i; i=$((i+1)); done; echo LAST"), opts);
        QVERIFY(waitDone(o, 20000));
        QVERIFY(o->text.startsWith(QLatin1String("FIRST\n")));
        QVERIFY(o->text.contains(QLatin1String("LAST\n[exit code 0]")));
        QVERIFY(o->text.contains(QLatin1String("bytes of output left out")));
        QVERIFY(o->text.toUtf8().size() < 4096 + 200);
    }

    void aTimeoutKillsEverythingItStarted()
    {
        QTemporaryDir dir;
        const QString pidFile = dir.filePath(QStringLiteral("pid"));
        ShellCommand sh;
        ShellCommand::Options opts;
        opts.timeoutMs = 500;
        QElapsedTimer t;
        t.start();
        // A child of the shell, which the shell itself waits for.
        auto o = start(sh, QStringLiteral("sleep 30 & echo $! > '%1'; wait").arg(pidFile), opts);
        QVERIFY(waitDone(o, 5000));
        QVERIFY(t.elapsed() < 4000);
        QVERIFY2(o->text.endsWith(QLatin1String("[stopped: still running after 0.5 seconds]")), qPrintable(o->text));
        QFile f(pidFile);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const pid_t child = f.readAll().trimmed().toInt();
        QVERIFY(child > 0);
        QTRY_VERIFY_WITH_TIMEOUT(::kill(child, 0) != 0, 3000); // gone, not left behind
    }

    void cancelKillsWithoutReporting()
    {
        ShellCommand sh;
        auto o = start(sh, QStringLiteral("sleep 30"));
        QTest::qWait(200);
        QCOMPARE(sh.runningCount(), 1);
        QElapsedTimer t;
        t.start();
        sh.cancelAll();
        QVERIFY(t.elapsed() < 2000);
        QCOMPARE(sh.runningCount(), 0);
        QTest::qWait(200);
        QVERIFY(!o->done);
    }

    void toolDefinitionNamesTheShell()
    {
        const QJsonObject fn = ShellCommand::toolDefinition(90).value(QStringLiteral("function")).toObject();
        QCOMPARE(fn.value(QStringLiteral("name")).toString(), QStringLiteral("run_command"));
        const QString d = fn.value(QStringLiteral("description")).toString();
        QVERIFY(d.contains(QStringLiteral("shell ") + ShellCommand::shellName()));
        QVERIFY(d.contains(QLatin1String("90 seconds")));
#ifdef Q_OS_MACOS
        QVERIFY(d.contains(QLatin1String("macOS (BSD")));
#endif
    }

    void shellIsAPosixOne()
    {
        const QByteArray old = qgetenv("SHELL");
        qputenv("SHELL", "/usr/bin/fish");
        QCOMPARE(ShellCommand::shellProgram(), QStringLiteral("/bin/sh"));
        qputenv("SHELL", "/bin/sh");
        QCOMPARE(ShellCommand::shellProgram(), QStringLiteral("/bin/sh"));
        qputenv("SHELL", old);
    }
};

QTEST_GUILESS_MAIN(TestShellCommand)
#include "test_shell_command.moc"
