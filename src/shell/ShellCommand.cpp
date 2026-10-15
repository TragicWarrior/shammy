#include "shell/ShellCommand.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QSysInfo>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

struct ShellCommand::Run
{
    QProcess *proc = nullptr;
    QTimer *timer = nullptr;
    Callback cb;
    qint64 limit = 0;
    QByteArray head;     // the first half of the limit
    QByteArray tail;     // the most recent output, up to the other half
    qint64 total = 0;    // every byte the command wrote
    bool timedOut = false;
    int timeoutMs = 0;
};

ShellCommand::ShellCommand(QObject *parent)
    : QObject(parent)
{
}

ShellCommand::~ShellCommand()
{
    cancelAll();
}

QString ShellCommand::shellProgram()
{
#ifdef Q_OS_WIN
    return QStringLiteral("cmd.exe");
#else
    // Commands are written for a POSIX shell, so only take $SHELL when it is one.
    const QString sh = qEnvironmentVariable("SHELL");
    const QString base = QFileInfo(sh).fileName();
    if ((base == QLatin1String("bash") || base == QLatin1String("zsh") || base == QLatin1String("sh")
         || base == QLatin1String("dash") || base == QLatin1String("ksh"))
        && QFileInfo(sh).isExecutable())
    {
        return sh;
    }
    return QStringLiteral("/bin/sh");
#endif
}

QString ShellCommand::shellName()
{
    return QFileInfo(shellProgram()).fileName();
}

QJsonObject ShellCommand::toolDefinition(int timeoutSeconds)
{
#if defined(Q_OS_MACOS)
    const QString system = QStringLiteral("macOS (BSD command-line tools, not GNU)");
#elif defined(Q_OS_WIN)
    const QString system = QStringLiteral("Windows");
#else
    const QString system = QSysInfo::prettyProductName();
#endif
    QJsonObject props;
    props.insert(QStringLiteral("command"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                             {QStringLiteral("description"), QStringLiteral("The command line to run.")}});
    props.insert(QStringLiteral("cwd"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                             {QStringLiteral("description"),
                              QStringLiteral("Optional folder to run it in. Defaults to the user's home folder.")}});
    QJsonObject params{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), props},
                       {QStringLiteral("required"), QJsonArray{QStringLiteral("command")}}};
    const QString desc = QStringLiteral(
                             "Run a command on the user's computer: %1, shell %2. Returns its output and exit "
                             "code. The user approves each command before it runs. Nothing can be typed in, so "
                             "a command that asks for input fails. Stopped after %3 seconds.")
                             .arg(system, shellName())
                             .arg(timeoutSeconds);
    QJsonObject fn{{QStringLiteral("name"), QStringLiteral("run_command")},
                   {QStringLiteral("description"), desc},
                   {QStringLiteral("parameters"), params}};
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("function")}, {QStringLiteral("function"), fn}};
}

void ShellCommand::killGroup(QProcess *p)
{
    if (p->state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_UNIX
    // The command ran in a session of its own, so this also reaches whatever it
    // started (a pipeline, a script's children), not only the shell.
    const qint64 pid = p->processId();
    if (pid > 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
    p->kill();
}

void ShellCommand::run(const QString &command, const Options &opts, const Callback &cb)
{
    QString cwd = opts.workingDirectory.trimmed();
    if (cwd == QLatin1String("~") || cwd.startsWith(QLatin1String("~/")))
        cwd = QDir::homePath() + cwd.mid(1);
    if (cwd.isEmpty())
        cwd = QDir::homePath();
    if (!QFileInfo(cwd).isDir())
    {
        cb({}, QStringLiteral("There is no folder %1.").arg(cwd));
        return;
    }

    auto *r = new Run;
    r->cb = cb;
    r->limit = qMax<qint64>(1024, opts.maxOutputBytes);
    r->timeoutMs = opts.timeoutMs;
    r->proc = new QProcess(this);
    r->proc->setProcessChannelMode(QProcess::MergedChannels);
    r->proc->setWorkingDirectory(cwd);
#ifdef Q_OS_UNIX
    r->proc->setChildProcessModifier([]() { ::setsid(); });
#endif
    m_runs << r;

    connect(r->proc, &QProcess::readyRead, this, [r]()
    {
        const QByteArray chunk = r->proc->readAll();
        r->total += chunk.size();
        const qint64 half = r->limit / 2;
        qint64 used = 0;
        if (r->head.size() < half)
        {
            used = qMin<qint64>(half - r->head.size(), chunk.size());
            r->head += chunk.left(used);
        }
        if (used < chunk.size())
        {
            r->tail += chunk.mid(used);
            if (r->tail.size() > r->limit - half)
                r->tail = r->tail.right(r->limit - half);
        }
    });
    connect(r->proc, &QProcess::finished, this, [this, r](int code, QProcess::ExitStatus status)
    {
        r->proc->readAll(); // nothing is left: readyRead has taken it
        QByteArray out = r->head;
        const qint64 shown = r->head.size() + r->tail.size();
        if (r->total > shown)
        {
            out += QStringLiteral("\n… (%1 bytes of output left out) …\n").arg(r->total - shown).toUtf8();
        }
        out += r->tail;
        QString text = QString::fromUtf8(out);
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
            text += QLatin1Char('\n');
        if (text.isEmpty())
            text = QStringLiteral("(no output)\n");
        if (r->timedOut)
            text += QStringLiteral("[stopped: still running after %1 seconds]").arg(r->timeoutMs / 1000.0);
        else if (status == QProcess::CrashExit)
            text += QStringLiteral("[ended by a signal]");
        else
            text += QStringLiteral("[exit code %1]").arg(code);
        finish(r, text, {});
    });
    connect(r->proc, &QProcess::errorOccurred, this, [this, r](QProcess::ProcessError e)
    {
        if (e == QProcess::FailedToStart)
            finish(r, {}, QStringLiteral("Could not start %1: %2").arg(shellProgram(), r->proc->errorString()));
    });

    r->timer = new QTimer(this);
    r->timer->setSingleShot(true);
    connect(r->timer, &QTimer::timeout, this, [r]()
    {
        r->timedOut = true;
        killGroup(r->proc); // `finished` follows and reports it
    });
    r->timer->start(qMax(1, opts.timeoutMs));

#ifdef Q_OS_WIN
    r->proc->start(shellProgram(), {QStringLiteral("/C"), command});
#else
    r->proc->start(shellProgram(), {QStringLiteral("-c"), command});
#endif
    r->proc->closeWriteChannel();
}

void ShellCommand::finish(Run *r, const QString &text, const QString &error)
{
    if (!m_runs.removeOne(r))
        return; // already reported
    r->timer->stop();
    r->timer->deleteLater();
    r->proc->disconnect(this);
    r->proc->deleteLater();
    const Callback cb = r->cb;
    delete r;
    cb(text, error);
}

void ShellCommand::cancelAll()
{
    const QList<Run *> runs = m_runs;
    m_runs.clear();
    for (Run *r : runs)
    {
        r->timer->stop();
        r->timer->deleteLater();
        r->proc->disconnect(this);
        killGroup(r->proc);
        r->proc->waitForFinished(1000);
        r->proc->deleteLater();
        delete r;
    }
}
