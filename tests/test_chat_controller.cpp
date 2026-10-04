#include "FakeTools.h"
#include "artifacts/PdfExtract.h"
#include "controllers/ChatController.h"
#include "controllers/McpController.h"
#include "controllers/ProjectController.h"
#include "controllers/SettingsController.h"
#include "mcp/McpHost.h"
#include "openai/OpenAiClient.h"
#include "persist/Store.h"

#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>
#include <memory>

// End-to-end checks of ChatController against a fake OpenAI-compatible server:
// regenerate/edit (private and saved chats), compaction, and tool results that
// arrive after their generation has ended.

namespace
{

class FakeOpenAi : public QObject
{
public:
    enum Kind
    {
        Text,
        ToolCall,
        Hold
    };
    struct Reply
    {
        Kind kind = Text;
        QString text;
        QString tool;
        QString args = QStringLiteral("{}");
    };
    using Handler = std::function<Reply(int index, const QJsonObject &body)>;

    static Reply text(const QString &t) { return {Text, t, {}, {}}; }
    static Reply toolCall(const QString &name, const QString &args = QStringLiteral("{}"))
    {
        return {ToolCall, {}, name, args};
    }
    static Reply hold() { return {Hold, {}, {}, {}}; }

    explicit FakeOpenAi(Handler h)
        : m_handler(std::move(h))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() { accept(); });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort());
    }

    // Bodies of every chat/completions request, in arrival order.
    QList<QJsonObject> requests;

    void releaseHeld(const QString &t)
    {
        const auto held = m_held;
        m_held.clear();
        for (const Held &h : held)
        {
            if (h.sock)
                respond(h.sock, h.stream, text(t));
        }
    }

private:
    struct Held
    {
        QPointer<QTcpSocket> sock;
        bool stream = true;
    };

    void accept()
    {
        while (QTcpSocket *s = m_server.nextPendingConnection())
        {
            s->setParent(this);
            connect(s, &QTcpSocket::readyRead, this, [this, s]() { onData(s); });
            connect(s, &QTcpSocket::disconnected, this, [this, s]()
            {
                m_buf.remove(s);
                for (int i = m_held.size() - 1; i >= 0; --i)
                {
                    if (m_held.at(i).sock == s)
                        m_held.removeAt(i);
                }
                s->deleteLater();
            });
        }
    }

    void onData(QTcpSocket *s)
    {
        QByteArray &buf = m_buf[s];
        buf += s->readAll();
        const int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0)
            return;
        const QByteArray head = buf.left(headEnd);
        int len = 0;
        const auto m = QRegularExpression(QStringLiteral("content-length:\\s*(\\d+)"),
                                          QRegularExpression::CaseInsensitiveOption)
                           .match(QString::fromLatin1(head));
        if (m.hasMatch())
            len = m.captured(1).toInt();
        if (buf.size() < headEnd + 4 + len)
            return;
        const QByteArray body = buf.mid(headEnd + 4, len);
        const QByteArray requestLine = head.left(head.indexOf("\r\n"));
        buf.clear();
        handle(s, requestLine, body);
    }

    void handle(QTcpSocket *s, const QByteArray &requestLine, const QByteArray &body)
    {
        if (requestLine.startsWith("GET") && requestLine.contains("/models"))
        {
            send(s, QStringLiteral("application/json"),
                 QStringLiteral("{\"object\":\"list\",\"data\":[{\"id\":\"test-model\"}]}").toUtf8());
            return;
        }
        if (!requestLine.contains("/chat/completions"))
        {
            s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
            s->disconnectFromHost();
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const int index = requests.size();
        requests.append(o);
        const Reply r = m_handler(index, o);
        const bool stream = o.value(QStringLiteral("stream")).toBool(true);
        if (r.kind == Hold)
        {
            m_held.append({s, stream});
            return;
        }
        respond(s, stream, r);
    }

    static QByteArray sse(const QJsonObject &o)
    {
        return "data: " + QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n\n";
    }

    void respond(QTcpSocket *s, bool stream, const Reply &r)
    {
        if (!stream)
        {
            const QJsonObject msg{{QStringLiteral("role"), QStringLiteral("assistant")},
                                  {QStringLiteral("content"), r.text}};
            const QJsonObject o{
                {QStringLiteral("choices"), QJsonArray{QJsonObject{{QStringLiteral("message"), msg}}}}};
            send(s, QStringLiteral("application/json"), QJsonDocument(o).toJson(QJsonDocument::Compact));
            return;
        }
        QByteArray out;
        QString finish = QStringLiteral("stop");
        if (r.kind == ToolCall)
        {
            const QJsonObject fn{{QStringLiteral("name"), r.tool}, {QStringLiteral("arguments"), r.args}};
            const QJsonObject tc{{QStringLiteral("index"), 0},
                                 {QStringLiteral("id"), QStringLiteral("call_1")},
                                 {QStringLiteral("type"), QStringLiteral("function")},
                                 {QStringLiteral("function"), fn}};
            out += sse({{QStringLiteral("choices"),
                         QJsonArray{QJsonObject{{QStringLiteral("delta"),
                                                 QJsonObject{{QStringLiteral("tool_calls"), QJsonArray{tc}}}}}}}});
            finish = QStringLiteral("tool_calls");
        }
        else
        {
            out += sse({{QStringLiteral("choices"),
                         QJsonArray{QJsonObject{{QStringLiteral("delta"),
                                                 QJsonObject{{QStringLiteral("content"), r.text}}}}}}});
        }
        out += sse({{QStringLiteral("choices"),
                     QJsonArray{QJsonObject{{QStringLiteral("delta"), QJsonObject{}},
                                            {QStringLiteral("finish_reason"), finish}}}}});
        out += "data: [DONE]\n\n";
        send(s, QStringLiteral("text/event-stream"), out, false);
    }

    void send(QTcpSocket *s, const QString &type, const QByteArray &body, bool withLength = true)
    {
        QByteArray head = "HTTP/1.1 200 OK\r\nContent-Type: " + type.toUtf8() + "\r\nConnection: close\r\n";
        if (withLength)
            head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        s->write(head + "\r\n" + body);
        s->flush();
        s->disconnectFromHost();
    }

    QTcpServer m_server;
    Handler m_handler;
    QHash<QTcpSocket *, QByteArray> m_buf;
    QList<Held> m_held;
};

// Accepts connections and never answers, so a fetch against it stays in flight.
class HoldServer : public QObject
{
public:
    HoldServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]()
        {
            while (QTcpSocket *s = m_server.nextPendingConnection())
            {
                s->setParent(this);
                ++connections;
                connect(s, &QTcpSocket::disconnected, this, [this, s]()
                {
                    ++disconnections;
                    s->deleteLater();
                });
            }
        });
    }
    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }
    int connections = 0;
    int disconnections = 0;

private:
    QTcpServer m_server;
};



template <class Pred>
bool waitFor(Pred pred, int ms = 8000)
{
    QElapsedTimer t;
    t.start();
    while (!pred())
    {
        if (t.elapsed() > ms)
            return false;
        QTest::qWait(10);
    }
    return true;
}

// Everything a ChatController needs, wired to a fake backend and (optionally) a
// fake MCP server that exposes one tool, "slow". `mcpTail` is the shell run once
// that tool is called; $GO is a file path the test can create to signal it.
struct Fixture
{
    QTemporaryDir dir;
    FakeOpenAi ai;
    Store store;
    OpenAiClient client;
    McpHost host;
    std::unique_ptr<McpController> mcp;
    std::unique_ptr<SettingsController> settings;
    std::unique_ptr<ProjectController> projects;
    std::unique_ptr<ChatController> chat;
    QString goFile;
    bool ok = false;

    explicit Fixture(FakeOpenAi::Handler handler, const QString &mcpTail = {})
        : ai(std::move(handler))
    {
        if (!dir.isValid() || !ai.listen() || !store.open(dir.filePath(QStringLiteral("t.db"))))
            return;
        goFile = dir.filePath(QStringLiteral("go"));

        QList<Backend> backends = store.backends();
        if (backends.size() < 2)
            return;
        backends[0].name = QStringLiteral("Fake");
        backends[0].baseUrl = ai.baseUrl();
        backends[1].enabled = false;
        store.upsertBackend(backends[0]);
        store.upsertBackend(backends[1]);
        store.setSetting(QStringLiteral("current_backend"), backends[0].id);

        host.setConfigPath(dir.filePath(QStringLiteral("mcp.json")));
        if (!mcpTail.isEmpty())
        {
            static const QString head = QStringLiteral(
                "read init; printf '%s\\n' '{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}'; "
                "read note; read list; "
                "printf '%s\\n' '{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"tools\":[{\"name\":\"slow\"}]}}'; "
                "read call; ");
            McpServerConfig c;
            c.name = QStringLiteral("fake");
            c.command = QStringLiteral("/bin/sh");
            c.args = {QStringLiteral("-c"), head + mcpTail};
            c.env.insert(QStringLiteral("GO"), goFile);
            host.replaceConfigs({c});
            if (!waitFor([this]() { return host.connectedCount() == 1; }))
                return;
            store.allowAlways(QStringLiteral("fake"), QStringLiteral("slow"));
        }

        mcp = std::make_unique<McpController>(&host, &store);
        settings = std::make_unique<SettingsController>(&store, &client);
        if (!waitFor([this]() { return !settings->loadingModels() && !settings->currentModel().isEmpty(); }))
            return;
        projects = std::make_unique<ProjectController>(&store);
        chat = std::make_unique<ChatController>(&store, &client, mcp.get(), projects.get(), settings.get());
        ok = true;
    }

    ~Fixture()
    {
        chat.reset();
        host.stopAll();
    }

    bool say(const QString &text)
    {
        chat->setComposerText(text);
        chat->send();
        return waitFor([this]() { return !chat->streaming(); });
    }

    QStringList roles() const
    {
        QStringList out;
        for (const ChatMessage &m : chat->messages()->all())
            out << m.role;
        return out;
    }
};

QStringList userTexts(const QJsonObject &request)
{
    QStringList out;
    for (const QJsonValue &v : request.value(QStringLiteral("messages")).toArray())
    {
        const QJsonObject m = v.toObject();
        if (m.value(QStringLiteral("role")).toString() == QLatin1String("user"))
            out << m.value(QStringLiteral("content")).toString();
    }
    return out;
}

QString systemText(const QJsonObject &request)
{
    const QJsonArray msgs = request.value(QStringLiteral("messages")).toArray();
    return msgs.isEmpty() ? QString() : msgs.at(0).toObject().value(QStringLiteral("content")).toString();
}

const auto numbered = [](int i, const QJsonObject &) { return FakeOpenAi::text(QStringLiteral("reply %1").arg(i + 1)); };

// Fake soffice and pdftotext in a scratch folder, and files to feed them.
struct ToolRig
{
    QTemporaryDir dir;
    QString pdftotext;
    QString soffice;
    ToolRig()
    {
        pdftotext = dir.filePath(QStringLiteral("pdftotext"));
        soffice = dir.filePath(QStringLiteral("soffice"));
        FakeTools::install(pdftotext, FakeTools::pdftotext);
        FakeTools::install(soffice, FakeTools::soffice);
    }
    QString file(const QString &name, const QByteArray &data = "%PDF-1.4\n")
    {
        const QString path = dir.filePath(name);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(data);
        return path;
    }
};
// These settings live in the QSettings file every test here shares.
struct ToolReset
{
    SettingsController &s;
    ~ToolReset()
    {
        s.setPdftotextBinaryPath({});
        s.setOfficeBinaryPath({});
        s.setModelContextFromText(s.currentBackendId(), QStringLiteral("test-model"), QStringLiteral("16k"));
    }
};
} // namespace

class TestChatController : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settingsDir;

private slots:
    void initTestCase()
    {
        if (!QFile::exists(QStringLiteral("/bin/sh")))
            QSKIP("needs /bin/sh to fake an MCP server");
        {
            // Some build sandboxes have no loopback interface.
            QTcpServer probe;
            if (!probe.listen(QHostAddress::LocalHost, 0))
                QSKIP("needs loopback networking to fake an OpenAI server");
        }
        // Keep QSettings and app data away from the real profile.
        QVERIFY(m_settingsDir.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settingsDir.path());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("shammy-test"));
        QCoreApplication::setApplicationName(QStringLiteral("shammy-test"));
    }

    // -- PDFs: read with pdftotext, and only while it is found -------------------

    void anAttachedPdfIsReadAndSentWithTheMessage()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.settings->setPdftotextBinaryPath(rig.pdftotext);
        f.settings->setModelContextFromText(f.settings->currentBackendId(), QStringLiteral("test-model"), QStringLiteral("256k"));
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("paper.pdf")));
        QVERIFY2(f.chat->errorBanner().isEmpty(), qPrintable(f.chat->errorBanner()));
        QCOMPARE(f.chat->pendingAttachments().size(), 1);
        QVERIFY(waitFor([&]() { return !f.chat->attachmentsBusy(); }));
        QCOMPARE(f.chat->pendingAttachments().size(), 1);
        QVERIFY(f.say(QStringLiteral("summarize this")));
        const QString sent = userTexts(f.ai.requests.last()).last();
        QVERIFY2(sent.contains(QLatin1String("summarize this")), qPrintable(sent));
        QVERIFY2(sent.contains(QLatin1String("Attached PDF `paper.pdf`")), qPrintable(sent));
        QVERIFY(sent.contains(QLatin1String("Quarterly Report")));
        QVERIFY(sent.contains(QLatin1String("North   120 135")));
        QVERIFY(!sent.contains(QLatin1String("Cut to fit")));
        QCOMPARE(f.chat->pendingAttachments().size(), 0);
    }

    void aPdfIsNotAttachedWhenPdftotextIsNotFound()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        const QString bad = rig.dir.filePath(QStringLiteral("no-such-pdftotext"));
        f.settings->setPdftotextBinaryPath(bad);
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("paper.pdf")));
        // Refused up front, with the reason, like a Word file without LibreOffice.
        QVERIFY2(f.chat->errorBanner().contains(QLatin1String("Can't attach `paper.pdf`")), qPrintable(f.chat->errorBanner()));
        QVERIFY2(f.chat->errorBanner().contains(QLatin1String("No usable pdftotext")), qPrintable(f.chat->errorBanner()));
        QVERIFY(f.chat->errorBanner().contains(bad));
        QCOMPARE(f.chat->pendingAttachments().size(), 0);
        QVERIFY(!f.chat->attachmentsBusy());
        // Everything else still attaches.
        f.chat->attachFile(rig.file(QStringLiteral("notes.txt"), "plain notes"));
        QCOMPARE(f.chat->pendingAttachments().size(), 1);
        QVERIFY(f.chat->errorBanner().isEmpty());
        QVERIFY(!f.chat->attachmentsBusy()); // a text file needs no conversion
    }

    void aWordFileIsNotAttachedWhenLibreOfficeIsNotFound()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        const QString bad = rig.dir.filePath(QStringLiteral("no-such-soffice"));
        f.settings->setOfficeBinaryPath(bad);
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("report.docx")));
        f.chat->attachFile(rig.file(QStringLiteral("sales.xlsx")));
        QVERIFY2(f.chat->errorBanner().contains(QLatin1String("Can't attach `sales.xlsx`")), qPrintable(f.chat->errorBanner()));
        QVERIFY2(f.chat->errorBanner().contains(QLatin1String("no usable LibreOffice or OpenOffice")), qPrintable(f.chat->errorBanner()));
        QCOMPARE(f.chat->pendingAttachments().size(), 0);
        QVERIFY(!f.chat->attachmentsBusy());
    }

    void aPdfWithNoTextIsDroppedBeforeSending()
    {
        // It used to be sent anyway, with "could not be read" inside the message.
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.settings->setPdftotextBinaryPath(rig.pdftotext);
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("scanned.pdf")));
        QVERIFY(waitFor([&]() { return !f.chat->attachmentsBusy(); }));
        QCOMPARE(f.chat->pendingAttachments().size(), 0);
        QVERIFY2(f.chat->errorBanner().contains(QLatin1String("Can't attach `scanned.pdf`")), qPrintable(f.chat->errorBanner()));
        QVERIFY(f.chat->errorBanner().contains(QLatin1String("OCR")));
        QCOMPARE(f.ai.requests.size(), 0);
    }

    // -- Attachments convert in the background; sending waits for them ----------

    void aWordAttachmentConvertsInTheBackgroundAndSendingWaits()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.settings->setOfficeBinaryPath(rig.soffice);
        f.chat->newChat();

        // The fake takes a second: attaching must not wait for it.
        QElapsedTimer t;
        t.start();
        f.chat->attachFile(rig.file(QStringLiteral("slow-report.docx")));
        QVERIFY2(t.elapsed() < 400, qPrintable(QString::number(t.elapsed())));
        QVERIFY(f.chat->attachmentsBusy());
        QCOMPARE(f.chat->pendingAttachments().size(), 1);
        QVERIFY2(f.chat->pendingAttachments().first().startsWith(QLatin1String("slow-report.docx (converting")),
                 qPrintable(f.chat->pendingAttachments().first()));

        // Sending now does nothing, and says so, so the composer keeps the draft.
        f.chat->setComposerText(QStringLiteral("what does it say?"));
        QVERIFY(!f.chat->send());
        QCOMPARE(f.chat->composerText(), QStringLiteral("what does it say?"));
        QCOMPARE(f.chat->pendingAttachments().size(), 1);
        QCOMPARE(f.ai.requests.size(), 0);
        QVERIFY(!f.chat->streaming());

        QVERIFY(waitFor([&]() { return !f.chat->attachmentsBusy(); }));
        QCOMPARE(f.chat->pendingAttachments().first().section(QStringLiteral(" — "), 0, 0), QStringLiteral("slow-report.docx"));
        QVERIFY(f.chat->send());
        QVERIFY(waitFor([&]() { return !f.chat->streaming(); }));
        const QString sent = userTexts(f.ai.requests.last()).last();
        QVERIFY2(sent.contains(QLatin1String("what does it say?")), qPrintable(sent));
        QVERIFY2(sent.contains(QLatin1String("Attached document `slow-report.docx`")), qPrintable(sent));
        QVERIFY(sent.contains(QLatin1String("Hello from the document")));
        QVERIFY(f.chat->composerText().isEmpty());
    }

    void aSpreadsheetAttachmentKeepsItsSheetNames()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.settings->setOfficeBinaryPath(rig.soffice);
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("sales.xlsx")));
        QVERIFY(waitFor([&]() { return !f.chat->attachmentsBusy(); }));
        QVERIFY2(f.chat->errorBanner().isEmpty(), qPrintable(f.chat->errorBanner()));
        QVERIFY(f.say(QStringLiteral("total it")));
        const QString sent = userTexts(f.ai.requests.last()).last();
        QCOMPARE(sent.count(QLatin1String("Attached spreadsheet `sales.xlsx` sheet `Data`:\n```csv\n")), 2);
        QVERIFY2(sent.contains(QLatin1String("name,qty\napple,3")), qPrintable(sent));
    }

    void aFailedConversionDropsTheAttachmentAndSaysWhy()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.settings->setOfficeBinaryPath(rig.soffice);
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("notes.txt"), "kept"));
        f.chat->attachFile(rig.file(QStringLiteral("broken.docx")));
        QCOMPARE(f.chat->pendingAttachments().size(), 2);
        QVERIFY(waitFor([&]() { return !f.chat->attachmentsBusy(); }));
        QCOMPARE(f.chat->pendingAttachments().size(), 1); // the text file stays
        QVERIFY2(f.chat->errorBanner().contains(QLatin1String("Can't attach `broken.docx`: conversion exploded")),
                 qPrintable(f.chat->errorBanner()));
    }

    void removingAnAttachmentWhileItConvertsIsSafe()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.settings->setOfficeBinaryPath(rig.soffice);
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("slow.docx")));
        f.chat->attachFile(rig.file(QStringLiteral("notes.txt"), "kept"));
        QVERIFY(f.chat->attachmentsBusy());
        f.chat->removeAttachment(0);
        QVERIFY(!f.chat->attachmentsBusy());
        QTest::qWait(1600); // the conversion finishes with nobody waiting for it
        QCOMPARE(f.chat->pendingAttachments().size(), 1);
        QVERIFY(f.chat->pendingAttachments().first().startsWith(QLatin1String("notes.txt")));
        QVERIFY2(f.chat->errorBanner().isEmpty(), qPrintable(f.chat->errorBanner()));
        QVERIFY(f.say(QStringLiteral("go")));
        QVERIFY(!userTexts(f.ai.requests.last()).last().contains(QLatin1String("Hello from the document")));
    }

    // -- Attachments are sized to the model's context window --------------------

    void attachmentsAreCutToFitASmallContext()
    {
        // 16K tokens: 26,214 bytes for all attachments in one message.
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.settings->setModelContextFromText(f.settings->currentBackendId(), QStringLiteral("test-model"), QStringLiteral("16k"));
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("one.txt"), QByteArray(20000, 'J')));
        f.chat->attachFile(rig.file(QStringLiteral("two.txt"), QByteArray(20000, 'K')));
        f.chat->attachFile(rig.file(QStringLiteral("three.txt"), QByteArray(20000, 'Z')));
        QVERIFY(f.say(QStringLiteral("read these")));
        const QString sent = userTexts(f.ai.requests.last()).last();
        QCOMPARE(sent.count(QLatin1Char('J')), 20000);        // whole
        QCOMPARE(sent.count(QLatin1Char('K')), 26214 - 20000); // what was left
        QCOMPARE(sent.count(QLatin1Char('Z')), 0);            // no room at all
        QCOMPARE(sent.count(QLatin1String("(Cut to fit the model's context window.)")), 1);
        QVERIFY2(sent.contains(QLatin1String("Attached file `three.txt` was left out")), qPrintable(sent.right(300)));
        QVERIFY(sent.toUtf8().size() < 27500);
    }

    void aLargeContextTakesMoreThanTheOldFixedCap()
    {
        // The old limit was 32 KB a file whatever the model.
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.settings->setModelContextFromText(f.settings->currentBackendId(), QStringLiteral("test-model"), QStringLiteral("256k"));
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("big.log"), QByteArray(100000, 'J')));
        f.chat->attachFile(rig.file(QStringLiteral("huge.log"), QByteArray(200000, 'K')));
        QVERIFY(f.say(QStringLiteral("read these")));
        const QString sent = userTexts(f.ai.requests.last()).last();
        QCOMPARE(sent.count(QLatin1Char('J')), 100000);
        QCOMPARE(sent.count(QLatin1Char('K')), 128 * 1024); // one file: at most half the 256 KB budget
        QCOMPARE(sent.count(QLatin1String("(Cut to fit the model's context window.)")), 1);
        QVERIFY(!sent.contains(QLatin1String("left out")));
    }

    void anAttachmentIsNeverCutInsideACharacter()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.chat->newChat();
        QByteArray emoji;
        for (int i = 0; i < 20000; ++i)
            emoji += "\xF0\x9F\x98\x80";
        f.chat->attachFile(rig.file(QStringLiteral("emoji.txt"), emoji));
        QVERIFY(f.say(QStringLiteral("look")));
        const QString sent = userTexts(f.ai.requests.last()).last();
        QVERIFY(sent.contains(QChar::fromUcs4(0x1F600)));
        QVERIFY(!sent.contains(QChar(0xFFFD)));
        QVERIFY(sent.contains(QLatin1String("Cut to fit")));
    }

    void aConvertedAttachmentIsCutToo()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.settings->setPdftotextBinaryPath(rig.pdftotext);
        f.chat->newChat();
        f.chat->attachFile(rig.file(QStringLiteral("huge.pdf"))); // megabytes of text
        QVERIFY(waitFor([&]() { return !f.chat->attachmentsBusy(); }, 30000));
        QVERIFY(f.say(QStringLiteral("summarize")));
        const QString sent = userTexts(f.ai.requests.last()).last();
        QVERIFY(sent.contains(QLatin1String("Attached PDF `huge.pdf`")));
        QVERIFY(sent.contains(QLatin1String("Cut to fit")));
        QVERIFY2(sent.toUtf8().size() < 27000, qPrintable(QString::number(sent.toUtf8().size())));
    }

    void anAttachmentDeletedBeforeSendingIsReported()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        f.chat->newChat();
        const QString path = rig.file(QStringLiteral("gone.txt"), "soon gone");
        f.chat->attachFile(path);
        QVERIFY(QFile::remove(path));
        QVERIFY(f.say(QStringLiteral("read it")));
        QVERIFY(userTexts(f.ai.requests.last()).last().contains(QLatin1String("Attached file `gone.txt` could not be read")));
    }

    // -- send() says whether it sent, so the composer can keep a draft ----------

    void sendReportsWhetherAnythingWasSent()
    {
        Fixture f([](int, const QJsonObject &) { return FakeOpenAi::hold(); });
        QVERIFY(f.ok);
        f.chat->newChat();
        QVERIFY(!f.chat->send()); // nothing typed, nothing attached
        f.chat->setComposerText(QStringLiteral("/help"));
        QVERIFY(f.chat->send()); // a command was run
        f.chat->setComposerText(QStringLiteral("first"));
        QVERIFY(f.chat->send());
        QVERIFY(f.chat->streaming());
        // A follow-up typed while the reply is still streaming is not swallowed.
        f.chat->setComposerText(QStringLiteral("follow-up"));
        QVERIFY(!f.chat->send());
        QCOMPARE(f.chat->composerText(), QStringLiteral("follow-up"));
        f.chat->stop();
    }

    void thePdftotextSettingIsNormalizedPersistedAndResolved()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        ToolRig rig;
        ToolReset reset{*f.settings};
        QCOMPARE(f.settings->pdftotextBinaryPath(), QString());

        f.settings->setPdftotextBinaryPath(QStringLiteral("  ") + QUrl::fromLocalFile(rig.pdftotext).toString() + QStringLiteral("  "));
        QCOMPARE(f.settings->pdftotextBinaryPath(), rig.pdftotext); // file: URL and padding removed
        QCOMPARE(f.settings->resolvePdftotext(rig.pdftotext), rig.pdftotext);
        QCOMPARE(f.settings->resolvePdftotext(rig.dir.path()), rig.pdftotext); // a folder holding it
        QVERIFY(f.settings->resolvePdftotext(rig.dir.filePath(QStringLiteral("nope"))).isEmpty());
        QCOMPARE(f.settings->pdftotextDetectedPath(), PdfExtract::pdftotextPath());

        QSignalSpy spy(f.settings.get(), &SettingsController::pdftotextBinaryPathChanged);
        f.settings->setPdftotextBinaryPath(rig.pdftotext); // same value: no signal
        QCOMPARE(spy.count(), 0);
        f.settings->setPdftotextBinaryPath({});
        QCOMPARE(spy.count(), 1);
        f.settings->setPdftotextBinaryPath(rig.pdftotext);
        SettingsController again(&f.store, &f.client);
        QCOMPARE(again.pdftotextBinaryPath(), rig.pdftotext); // read back from the stored settings
    }

    // -- Web fetch limits (Settings) ---------------------------------------------

    void webFetchLimitsAreClampedPersistedAndFeedTheFetchOptions()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        SettingsController &s = *f.settings;
        // The QSettings file is shared by every test here, so put the defaults back.
        struct Reset
        {
            SettingsController &s;
            ~Reset()
            {
                s.setWebFetchStallSeconds(WebFetchOptions::kDefaultStallSeconds);
                s.setWebFetchTimeoutSeconds(WebFetchOptions::kDefaultTotalSeconds);
            }
        } reset{s};

        QCOMPARE(s.webFetchStallSeconds(), 30);
        QCOMPARE(s.webFetchTimeoutSeconds(), 300);
        QCOMPARE(s.webFetchStallDefault(), 30);
        QCOMPARE(s.webFetchTimeoutDefault(), 300);

        s.setWebFetchStallSeconds(1);
        QCOMPARE(s.webFetchStallSeconds(), s.webFetchStallMin());
        s.setWebFetchStallSeconds(999999);
        QCOMPARE(s.webFetchStallSeconds(), s.webFetchStallMax());
        s.setWebFetchTimeoutSeconds(1);
        QCOMPARE(s.webFetchTimeoutSeconds(), s.webFetchTimeoutMin());
        s.setWebFetchTimeoutSeconds(999999);
        QCOMPARE(s.webFetchTimeoutSeconds(), s.webFetchTimeoutMax());

        s.setWebFetchStallSeconds(45);
        s.setWebFetchTimeoutSeconds(600);
        WebFetchOptions o = s.webFetchOptions();
        QCOMPARE(o.stallMs, 45000);
        QCOMPARE(o.totalMs, 600000);
        QCOMPARE(o.maxBytes, WebFetchOptions::kDefaultMaxBytes);

        // Whatever is typed into either field, the total is never shorter than the stall time.
        s.setWebFetchStallSeconds(300);
        s.setWebFetchTimeoutSeconds(30);
        o = s.webFetchOptions();
        QCOMPARE(o.stallMs, 300000);
        QCOMPARE(o.totalMs, 300000);

        // Changes are announced once, and only when the value really changes.
        s.setWebFetchStallSeconds(45);
        QSignalSpy spy(&s, &SettingsController::webFetchChanged);
        s.setWebFetchStallSeconds(45);
        QCOMPARE(spy.count(), 0);
        s.setWebFetchStallSeconds(60);
        QCOMPARE(spy.count(), 1);
        s.setWebFetchTimeoutSeconds(90);
        QCOMPARE(spy.count(), 2);

        // A fresh controller reads them back from the stored settings.
        SettingsController again(&f.store, &f.client);
        QCOMPARE(again.webFetchStallSeconds(), 60);
        QCOMPARE(again.webFetchTimeoutSeconds(), 90);
    }

    void webFetchLimitsIgnoreGarbageInTheStoredSettings()
    {
        {
            QSettings raw;
            raw.setValue(QStringLiteral("webFetchStallSeconds"), -5);
            raw.setValue(QStringLiteral("webFetchTimeoutSeconds"), QStringLiteral("soon"));
            raw.sync();
        }
        Fixture f(numbered);
        QVERIFY(f.ok);
        QCOMPARE(f.settings->webFetchStallSeconds(), f.settings->webFetchStallMin());
        QCOMPARE(f.settings->webFetchTimeoutSeconds(), f.settings->webFetchTimeoutMin());
        f.settings->setWebFetchStallSeconds(WebFetchOptions::kDefaultStallSeconds);
        f.settings->setWebFetchTimeoutSeconds(WebFetchOptions::kDefaultTotalSeconds);
    }

    // -- B1: Regenerate / Edit in a private chat -------------------------------

    void privateRegenerateKeepsTheConversation()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        f.chat->newPrivateChat();
        QVERIFY(f.say(QStringLiteral("hello there")));
        QCOMPARE(f.roles(), (QStringList{"user", "assistant"}));

        f.chat->regenerate();
        QVERIFY(waitFor([&]() { return !f.chat->streaming() && f.ai.requests.size() == 2; }));

        // The new request still carries the user's message.
        QCOMPARE(userTexts(f.ai.requests.at(1)), QStringList{QStringLiteral("hello there")});
        QCOMPARE(f.roles(), (QStringList{"user", "assistant"}));
        QCOMPARE(f.chat->messages()->all().last().content, QStringLiteral("reply 2"));
        // ... and the chat is still private: nothing was written to history.
        QVERIFY(f.chat->privateSession());
        QVERIFY(f.store.conversations().isEmpty());
        QVERIFY(f.store.messages(f.chat->conversationId()).isEmpty());
    }

    void privateEditKeepsEarlierTurns()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        f.chat->newPrivateChat();
        QVERIFY(f.say(QStringLiteral("first question")));
        QVERIFY(f.say(QStringLiteral("second question")));
        QCOMPARE(f.roles(), (QStringList{"user", "assistant", "user", "assistant"}));

        const QString secondId = f.chat->messages()->all().at(2).id;
        f.chat->editAndResend(secondId, QStringLiteral("edited question"));
        QVERIFY(waitFor([&]() { return !f.chat->streaming() && f.ai.requests.size() == 3; }));

        const QJsonObject req = f.ai.requests.at(2);
        QCOMPARE(userTexts(req), (QStringList{"first question", "edited question"}));
        QCOMPARE(f.roles(), (QStringList{"user", "assistant", "user", "assistant"}));
        QCOMPARE(f.chat->messages()->all().at(2).content, QStringLiteral("edited question"));
        QCOMPARE(f.chat->messages()->all().at(3).content, QStringLiteral("reply 3"));
        QVERIFY(f.chat->privateSession());
        QVERIFY(f.store.conversations().isEmpty());
    }

    void privateEditOfFirstMessageStartsOver()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        f.chat->newPrivateChat();
        QVERIFY(f.say(QStringLiteral("typo")));
        f.chat->editAndResend(f.chat->messages()->all().at(0).id, QStringLiteral("fixed"));
        QVERIFY(waitFor([&]() { return !f.chat->streaming() && f.ai.requests.size() == 2; }));
        QCOMPARE(userTexts(f.ai.requests.at(1)), QStringList{QStringLiteral("fixed")});
        QCOMPARE(f.roles(), (QStringList{"user", "assistant"}));
        QVERIFY(f.chat->privateSession());
    }

    // -- Saved chats: same operations, deleted by id ----------------------------

    void savedRegenerateAndEdit()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        f.chat->newChat();
        QVERIFY(f.say(QStringLiteral("first question")));
        QVERIFY(f.say(QStringLiteral("second question")));
        const QString convId = f.chat->conversationId();
        QCOMPARE(f.store.messages(convId).size(), 4);

        f.chat->regenerate();
        QVERIFY(waitFor([&]() { return !f.chat->streaming() && f.ai.requests.size() == 3; }));
        QCOMPARE(userTexts(f.ai.requests.at(2)), (QStringList{"first question", "second question"}));
        auto stored = f.store.messages(convId);
        QCOMPARE(stored.size(), 4);
        QCOMPARE(stored.last().content, QStringLiteral("reply 3"));

        const QString secondId = stored.at(2).id;
        f.chat->editAndResend(secondId, QStringLiteral("edited"));
        QVERIFY(waitFor([&]() { return !f.chat->streaming() && f.ai.requests.size() == 4; }));
        QCOMPARE(userTexts(f.ai.requests.at(3)), (QStringList{"first question", "edited"}));
        stored = f.store.messages(convId);
        QCOMPARE(stored.size(), 4);
        QCOMPARE(stored.at(2).content, QStringLiteral("edited"));
        QCOMPARE(stored.at(3).content, QStringLiteral("reply 4"));
    }

    // -- B2: a compaction summary must survive Regenerate / Edit ----------------

    void compactionSummarySortsBeforeTheKeptTurns()
    {
        Fixture f([](int i, const QJsonObject &body)
                  {
                      if (!body.value(QStringLiteral("stream")).toBool(true))
                          return FakeOpenAi::text(QStringLiteral("SUMMARY-OF-EARLIER"));
                      return FakeOpenAi::text(QStringLiteral("reply %1").arg(i + 1));
                  });
        QVERIFY(f.ok);
        f.chat->newChat();
        QVERIFY(f.say(QStringLiteral("first question")));
        QVERIFY(f.say(QStringLiteral("second question")));
        const QString convId = f.chat->conversationId();

        f.chat->compact();
        QVERIFY(waitFor([&]() { return f.chat->compactStatus().startsWith(QLatin1String("Compacted")); }));

        const auto stored = f.store.messages(convId);
        QCOMPARE(stored.size(), 3);
        QCOMPARE(stored.at(0).role, QStringLiteral("system"));
        QVERIFY(stored.at(0).content.contains(QLatin1String("SUMMARY-OF-EARLIER")));
        QCOMPARE(stored.at(1).role, QStringLiteral("user"));
        QVERIFY(stored.at(0).createdAt < stored.at(1).createdAt);
    }

    void compactionSummarySurvivesRegenerateAndEdit()
    {
        Fixture f([](int i, const QJsonObject &body)
                  {
                      if (!body.value(QStringLiteral("stream")).toBool(true))
                          return FakeOpenAi::text(QStringLiteral("SUMMARY-OF-EARLIER"));
                      return FakeOpenAi::text(QStringLiteral("reply %1").arg(i + 1));
                  });
        QVERIFY(f.ok);
        f.chat->newChat();
        QVERIFY(f.say(QStringLiteral("first question")));
        QVERIFY(f.say(QStringLiteral("second question")));
        const QString convId = f.chat->conversationId();
        f.chat->compact();
        QVERIFY(waitFor([&]() { return f.chat->compactStatus().startsWith(QLatin1String("Compacted")); }));
        QCOMPARE(f.ai.requests.size(), 3); // two turns + the summary request

        f.chat->regenerate();
        QVERIFY(waitFor([&]() { return !f.chat->streaming() && f.ai.requests.size() == 4; }));
        QVERIFY(systemText(f.ai.requests.at(3)).contains(QLatin1String("SUMMARY-OF-EARLIER")));
        QCOMPARE(userTexts(f.ai.requests.at(3)), QStringList{QStringLiteral("second question")});
        auto stored = f.store.messages(convId);
        QCOMPARE(stored.size(), 3);
        QCOMPARE(stored.at(0).role, QStringLiteral("system"));
        QCOMPARE(stored.at(2).content, QStringLiteral("reply 4"));

        f.chat->editAndResend(stored.at(1).id, QStringLiteral("edited question"));
        QVERIFY(waitFor([&]() { return !f.chat->streaming() && f.ai.requests.size() == 5; }));
        QVERIFY(systemText(f.ai.requests.at(4)).contains(QLatin1String("SUMMARY-OF-EARLIER")));
        QCOMPARE(userTexts(f.ai.requests.at(4)), QStringList{QStringLiteral("edited question")});
        stored = f.store.messages(convId);
        QCOMPARE(stored.size(), 3);
        QCOMPARE(stored.at(0).role, QStringLiteral("system"));
        QCOMPARE(stored.at(1).content, QStringLiteral("edited question"));
    }

    // -- B3 (in the chat): a dying MCP server must not hang the tool round ------

    void toolServerExitingMidCallDoesNotHangTheChat()
    {
        Fixture f([](int i, const QJsonObject &)
                  {
                      if (i == 0)
                          return FakeOpenAi::toolCall(QStringLiteral("slow"));
                      return FakeOpenAi::text(QStringLiteral("recovered"));
                  },
                  QStringLiteral("exit 0"));
        QVERIFY(f.ok);
        f.chat->newChat();
        QVERIFY(f.say(QStringLiteral("use the tool")));

        QCOMPARE(f.ai.requests.size(), 2);
        QVERIFY(f.roles().contains(QStringLiteral("tool")));
        QString toolText;
        for (const ChatMessage &m : f.chat->messages()->all())
        {
            if (m.role == QLatin1String("tool"))
                toolText = m.content;
        }
        QVERIFY2(toolText.contains(QLatin1String("exited")), qPrintable(toolText));
        QCOMPARE(f.chat->messages()->all().last().content, QStringLiteral("recovered"));
    }

    // -- Project files are sized to the model's context window -------------------

    void projectFilesInThePromptFitTheModelsContext()
    {
        Fixture f(numbered);
        QVERIFY(f.ok);
        const QString backend = f.settings->currentBackendId();
        // The context size is stored in the QSettings file every test here shares.
        struct Reset
        {
            SettingsController &s;
            QString backend;
            ~Reset() { s.setModelContextFromText(backend, QStringLiteral("test-model"), QStringLiteral("16k")); }
        } reset{*f.settings, backend};

        f.projects->createProject(QStringLiteral("Prompt size"));
        const QString projectId = f.projects->currentProjectId();
        // Delete the project (and so its files) even if a check below fails.
        struct DeleteProject
        {
            ProjectController &p;
            QString id;
            ~DeleteProject() { p.deleteProject(id); }
        } cleanup{*f.projects, projectId};
        for (const char *name : {"a.txt", "b.txt", "c.txt"})
            f.projects->addFileFromContent(QString::fromLatin1(name), QByteArray(20000, name[0]));

        // A 16K window: the three 20 KB files do not all fit in 40% of it.
        f.settings->setModelContextFromText(backend, QStringLiteral("test-model"), QStringLiteral("16k"));
        f.chat->newChat();
        QVERIFY(f.say(QStringLiteral("hello")));
        const QString small = systemText(f.ai.requests.last());
        QVERIFY2(small.contains(QLatin1String("left out to fit the context window")), qPrintable(small.left(400)));
        QVERIFY2(small.toUtf8().size() < 30000, qPrintable(QString::number(small.toUtf8().size())));
        QVERIFY(small.contains(QLatin1String("## a.txt")));
        QVERIFY(!small.contains(QLatin1String("## c.txt")));

        // A 256K window takes all of it.
        f.settings->setModelContextFromText(backend, QStringLiteral("test-model"), QStringLiteral("256k"));
        f.chat->newChat();
        QVERIFY(f.say(QStringLiteral("hello again")));
        const QString big = systemText(f.ai.requests.last());
        QVERIFY(!big.contains(QLatin1String("left out")));
        QVERIFY(big.contains(QLatin1String("## a.txt")));
        QVERIFY(big.contains(QLatin1String("## b.txt")));
        QVERIFY(big.contains(QLatin1String("## c.txt")));
        QVERIFY(big.toUtf8().size() > 60000);

        f.projects->deleteProject(projectId); // and its files go with it
        QVERIFY(!QFileInfo::exists(ProjectController::projectsRoot() + QLatin1Char('/') + projectId));
        cleanup.id.clear(); // already gone
    }

    // -- Stop cancels a web fetch that is still downloading ---------------------

    void stopCancelsAWebFetchInFlight()
    {
        HoldServer hold;
        QVERIFY(hold.listen());
        const QString url = QStringLiteral("http://127.0.0.1:%1/page").arg(hold.port());
        Fixture f([url](int i, const QJsonObject &)
                  {
                      if (i == 0)
                          return FakeOpenAi::toolCall(QStringLiteral("web_fetch"),
                                                      QStringLiteral("{\"url\":\"%1\"}").arg(url));
                      return FakeOpenAi::text(QStringLiteral("carried on"));
                  });
        QVERIFY(f.ok);
        // Let the fetcher reach the local server (loopback is refused by default).
        f.chat->webSearch()->setAddressPolicy(
            [](const QHostAddress &a) { return a.isLoopback() || WebSearch::addressAllowed(a); });

        f.chat->newChat();
        f.chat->setComposerText(QStringLiteral("read that page"));
        f.chat->send();
        QVERIFY(waitFor([&]() { return hold.connections == 1; }));
        QVERIFY(f.chat->streaming());
        QCOMPARE(hold.disconnections, 0);

        f.chat->stop();
        QVERIFY(!f.chat->streaming());
        // The download was abandoned, not left running until the stall timeout.
        QVERIFY(waitFor([&]() { return hold.disconnections == 1; }, 2000));
        QVERIFY(!f.roles().contains(QStringLiteral("tool")));

        // The chat is usable afterwards.
        QVERIFY(f.say(QStringLiteral("hello again")));
        QCOMPARE(f.chat->messages()->all().last().content, QStringLiteral("carried on"));
    }

    // -- B4: a tool result that outlives its generation is dropped --------------

    void staleToolResultDoesNotDisturbTheNextReply()
    {
        // Request 0 asks for the slow tool; request 1 (the next message) is held
        // open; request 2 must never happen.
        Fixture f([](int i, const QJsonObject &)
                  {
                      if (i == 0)
                          return FakeOpenAi::toolCall(QStringLiteral("slow"));
                      if (i == 1)
                          return FakeOpenAi::hold();
                      return FakeOpenAi::text(QStringLiteral("unexpected"));
                  },
                  // Wait for the test's signal, then reply.
                  QStringLiteral("while [ ! -f \"$GO\" ]; do sleep 0.02; done; "
                                 "id=$(printf '%s' \"$call\" | sed 's/.*\"id\":\\([0-9]*\\).*/\\1/'); "
                                 "printf '{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"LATE\"}]}}\\n' \"$id\"; "
                                 "exec sleep 30"));
        QVERIFY(f.ok);
        f.chat->newChat();
        f.chat->setComposerText(QStringLiteral("use the tool"));
        f.chat->send();
        // The tool call has been dispatched once the assistant message carries it.
        QVERIFY(waitFor([&]() { return !f.chat->messages()->all().isEmpty()
                                    && !f.chat->messages()->all().last().toolCallsJson.isEmpty(); }));

        f.chat->stop();
        QVERIFY(!f.chat->streaming());

        f.chat->setComposerText(QStringLiteral("second message"));
        f.chat->send();
        QVERIFY(waitFor([&]() { return f.ai.requests.size() == 2; }));
        QVERIFY(f.chat->streaming());

        // Now the abandoned tool call finally answers.
        QFile go(f.goFile);
        QVERIFY(go.open(QIODevice::WriteOnly));
        go.close();
        QTest::qWait(700);

        // It must not have been folded into the new reply or restarted the stream.
        QCOMPARE(f.ai.requests.size(), 2);
        QVERIFY(f.chat->streaming());
        QVERIFY(!f.roles().contains(QStringLiteral("tool")));

        f.ai.releaseHeld(QStringLiteral("final answer"));
        QVERIFY(waitFor([&]() { return !f.chat->streaming(); }));
        QCOMPARE(f.ai.requests.size(), 2);
        QCOMPARE(f.chat->messages()->all().last().content, QStringLiteral("final answer"));
        QVERIFY(!f.roles().contains(QStringLiteral("tool")));
    }
};

QTEST_GUILESS_MAIN(TestChatController)
#include "test_chat_controller.moc"
