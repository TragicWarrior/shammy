#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class QProcess;
class QTimer;

// Runs one command line for the model's run_command tool, in the user's shell
// (bash or zsh from $SHELL, else /bin/sh; cmd.exe on Windows), with stdin
// closed so that nothing can wait for typed input. The output (stdout and
// stderr together) is kept to a byte limit, its start and its end, and the
// whole process group is killed on a timeout or a cancel.
class ShellCommand : public QObject
{
    Q_OBJECT
public:
    static constexpr int kDefaultTimeoutSeconds = 120;
    static constexpr qint64 kDefaultMaxOutputBytes = 32 * 1024;

    struct Options
    {
        QString workingDirectory; // empty: the home folder
        int timeoutMs = kDefaultTimeoutSeconds * 1000;
        qint64 maxOutputBytes = kDefaultMaxOutputBytes;
    };
    // `text` is what the model is given: the output and how the command ended.
    // `error` is set instead when it could not be run at all.
    using Callback = std::function<void(const QString &text, const QString &error)>;

    explicit ShellCommand(QObject *parent = nullptr);
    ~ShellCommand() override;

    void run(const QString &command, const Options &opts, const Callback &cb);
    // Kills every command still running. Their callbacks are not called.
    void cancelAll();
    int runningCount() const { return m_runs.size(); }

    // The shell that runs commands, and a short name for it ("zsh").
    static QString shellProgram();
    static QString shellName();
    // The run_command tool, in OpenAI function format. It names the system and
    // the shell, so that the model writes commands for them (BSD tools on macOS).
    static QJsonObject toolDefinition(int timeoutSeconds = kDefaultTimeoutSeconds);

private:
    struct Run;
    void finish(Run *r, const QString &text, const QString &error);
    static void killGroup(QProcess *p);
    QList<Run *> m_runs;
};
