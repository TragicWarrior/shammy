#pragma once

#include "openai/ChatTypes.h"

#include <QElapsedTimer>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>

class McpClient : public QObject
{
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Connected, Error };

    explicit McpClient(McpServerConfig cfg, QObject *parent = nullptr);
    ~McpClient() override;

    const McpServerConfig &config() const { return m_cfg; }
    State state() const { return m_state; }
    QString errorString() const { return m_error; }
    QList<McpTool> tools() const { return m_tools; }
    QString logText() const { return m_log; }

    void start();
    void stop();
    // A tool call that gets no reply within this long is failed so the chat is
    // never left waiting on a server that has gone quiet. Settable for tests.
    void setCallTimeoutMs(int ms) { m_callTimeoutMs = ms; }
    int callTimeoutMs() const { return m_callTimeoutMs; }
    void callTool(const QString &name, const QJsonObject &args,
                  const std::function<void(QJsonValue result, QString error)> &cb);

signals:
    void stateChanged();
    void toolsChanged();
    void logUpdated();

private:
    void setState(State s);
    void onStdout();
    void onStderr();
    void onFinished(int code, QProcess::ExitStatus st);
    void send(const QByteArray &line);
    void handshakeTimeout();
    void handleMessage(const QByteArray &line);
    void failPending(const QString &reason);
    void expireCall(int id);

    McpServerConfig m_cfg;
    QProcess m_proc;
    State m_state = State::Stopped;
    QString m_error;
    QByteArray m_stdoutBuf;
    QString m_log;
    QList<McpTool> m_tools;
    int m_nextId = 10;
    int m_callTimeoutMs = 300000;
    QTimer m_handshakeTimer;
    struct Pending
    {
        std::function<void(QJsonValue, QString)> cb;
    };
    QHash<int, Pending> m_pending;
};
