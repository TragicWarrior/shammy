#include "mcp/McpClient.h"
#include "mcp/JsonRpc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>

McpClient::McpClient(McpServerConfig cfg, QObject *parent)
    : QObject(parent)
    , m_cfg(std::move(cfg))
{
    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &McpClient::onStdout);
    connect(&m_proc, &QProcess::readyReadStandardError, this, &McpClient::onStderr);
    connect(&m_proc, &QProcess::finished, this, &McpClient::onFinished);
    m_handshakeTimer.setSingleShot(true);
    m_handshakeTimer.setInterval(30000);
    connect(&m_handshakeTimer, &QTimer::timeout, this, &McpClient::handshakeTimeout);
}

McpClient::~McpClient()
{
    stop();
}

void McpClient::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void McpClient::send(const QByteArray &line)
{
    m_proc.write(line);
}

void McpClient::start()
{
    if (m_state == State::Starting || m_state == State::Connected)
        return;
    m_error.clear();
    m_tools.clear();
    m_stdoutBuf.clear();
    m_pending.clear();
    setState(State::Starting);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = m_cfg.env.begin(); it != m_cfg.env.end(); ++it)
        env.insert(it.key(), it.value());
    m_proc.setProcessEnvironment(env);
    if (!m_cfg.cwd.isEmpty())
        m_proc.setWorkingDirectory(m_cfg.cwd);
    m_proc.start(m_cfg.command, m_cfg.args);
    if (!m_proc.waitForStarted(5000))
    {
        m_error = m_proc.errorString();
        setState(State::Error);
        return;
    }
    const QJsonObject params
    {
        {QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05")},
        {QStringLiteral("capabilities"), QJsonObject{}},
        {QStringLiteral("clientInfo"),
         QJsonObject
         {
             {QStringLiteral("name"), QStringLiteral("shammy")},
             {QStringLiteral("version"), QStringLiteral("0.1.0")},
         }},
    };
    send(JsonRpc::encodeRequest(1, QStringLiteral("initialize"), params));
    m_handshakeTimer.start();
}

void McpClient::stop()
{
    m_handshakeTimer.stop();
    m_pending.clear();
    if (m_proc.state() != QProcess::NotRunning)
    {
        m_proc.terminate();
        if (!m_proc.waitForFinished(1500))
            m_proc.kill();
    }
    setState(State::Stopped);
}

void McpClient::handshakeTimeout()
{
    if (m_state != State::Starting)
        return;
    m_error = QStringLiteral("MCP handshake timed out");
    m_proc.kill();
    setState(State::Error);
}

void McpClient::onStderr()
{
    const QString chunk = QString::fromUtf8(m_proc.readAllStandardError());
    m_log += chunk;
    if (m_log.size() > 64 * 1024)
        m_log = m_log.right(48 * 1024);
    emit logUpdated();
}

void McpClient::onFinished(int, QProcess::ExitStatus)
{
    m_handshakeTimer.stop();
    if (m_state == State::Stopped)
        return;
    if (m_state == State::Connected)
    {
        m_error = QStringLiteral("MCP server exited");
        m_tools.clear();
        emit toolsChanged();
        setState(State::Error);
    }
}

void McpClient::onStdout()
{
    m_stdoutBuf += m_proc.readAllStandardOutput();
    while (true)
    {
        const int nl = m_stdoutBuf.indexOf('\n');
        if (nl < 0)
            break;
        const QByteArray line = m_stdoutBuf.left(nl + 1);
        m_stdoutBuf.remove(0, nl + 1);
        handleMessage(line);
    }
}

void McpClient::handleMessage(const QByteArray &line)
{
    const JsonRpc::Message msg = JsonRpc::parseLine(line);
    if (!msg.valid)
        return;

    if (msg.isResponse)
    {
        const int id = msg.id.toInt();
        if (id == 1 && m_state == State::Starting)
        {
            if (!msg.error.isEmpty())
            {
                m_error = msg.error.value(QStringLiteral("message")).toString();
                if (m_error.isEmpty())
                    m_error = QStringLiteral("initialize failed");
                m_handshakeTimer.stop();
                setState(State::Error);
                return;
            }
            send(JsonRpc::encodeNotification(QStringLiteral("notifications/initialized")));
            send(JsonRpc::encodeRequest(2, QStringLiteral("tools/list")));
            return;
        }
        if (id == 2 && m_state == State::Starting)
        {
            m_handshakeTimer.stop();
            m_tools.clear();
            const QJsonArray tools = msg.result.toObject().value(QStringLiteral("tools")).toArray();
            for (const auto &v : tools)
            {
                const QJsonObject t = v.toObject();
                McpTool tool;
                tool.server = m_cfg.name;
                tool.name = t.value(QStringLiteral("name")).toString();
                tool.exposedName = tool.name;
                tool.description = t.value(QStringLiteral("description")).toString();
                tool.inputSchema = t.value(QStringLiteral("inputSchema")).toObject();
                if (!tool.name.isEmpty())
                    m_tools.append(tool);
            }
            setState(State::Connected);
            emit toolsChanged();
            return;
        }
        if (m_pending.contains(id))
        {
            auto cb = m_pending.take(id).cb;
            if (!msg.error.isEmpty())
            {
                QString e = msg.error.value(QStringLiteral("message")).toString();
                if (e.isEmpty())
                    e = QJsonDocument(msg.error).toJson(QJsonDocument::Compact);
                cb({}, e);
            }
            else
            {
                cb(msg.result, {});
            }
        }
        return;
    }
}

void McpClient::callTool(const QString &name, const QJsonObject &args,
                         const std::function<void(QJsonValue, QString)> &cb)
{
    if (m_state != State::Connected)
    {
        cb({}, QStringLiteral("server not connected"));
        return;
    }
    const int id = ++m_nextId;
    if (m_nextId < 10)
        m_nextId = 10;
    m_pending.insert(id, Pending{cb});
    const QJsonObject params
    {
        {QStringLiteral("name"), name},
        {QStringLiteral("arguments"), args},
    };
    send(JsonRpc::encodeRequest(id, QStringLiteral("tools/call"), params));
}
