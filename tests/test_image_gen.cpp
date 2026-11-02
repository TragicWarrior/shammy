#include "imagegen/ImageGen.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

// Tests for ImageGen: preset mapping, aspect mapping, tool definition, progress
// text, tool-result text, error mapping, imageForToolResult round trip, and the
// full async job lifecycle against a fake local HTTP server.

namespace
{

// Minimal fake HTTP server for tests. Supports dynamic poll responses via
// a per-path counter that tests can configure.
class TestServer : public QObject
{
public:
    struct Route
    {
        int status = 200;
        QByteArray reason = "OK";
        QByteArray contentType = "application/json";
        QByteArray body;
    };

    TestServer() { connect(&m_server, &QTcpServer::newConnection, this, [this]() { accept(); }); }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }
    QString url(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(path);
    }

    QHash<QString, Route> routes;
    QHash<QString, int> hits;

    // Per-path poll counter for dynamic poll responses.
    // Tests set pollResponses[path] = {resp1, resp2, ...}.
    QHash<QString, QList<QByteArray>> pollResponses;

    // Binary image data for /image endpoints.
    QByteArray imageBytes;

    // Tracks DELETE /v1/jobs/{id} requests (sent by cancel()).
    int deleteHits = 0;

    // Captured POST body for the submit endpoint.
    QByteArray lastSubmitBody;

private:
    void accept()
    {
        while (QTcpSocket *s = m_server.nextPendingConnection())
        {
            s->setParent(this);
            connect(s, &QTcpSocket::readyRead, this, [this, s]() { onData(s); });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }

    void onData(QTcpSocket *s)
    {
        QByteArray &buf = m_buf[s];
        buf += s->readAll();
        const int end = buf.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QList<QByteArray> parts = buf.left(buf.indexOf("\r\n")).split(' ');
        // Capture the POST body for the submit endpoint before consuming.
        if (parts.size() >= 2 && parts[0] == "POST" &&
            parts[1] == "/v1/images/generations")
        {
            lastSubmitBody = buf.mid(end + 4);
        }
        // Consume the entire request (headers + body) so the socket is ready for the response.
        m_buf.remove(s);
        if (parts.size() >= 2)
            serve(s, QString::fromLatin1(parts.at(0)), QString::fromLatin1(parts.at(1)));
    }

    void serve(QTcpSocket *s, const QString &method, const QString &path)
    {
        if (method == "DELETE")
        {
            // Only count DELETE to /v1/jobs/<job_id> with a valid id.
            if (path.startsWith(QStringLiteral("/v1/jobs/")))
            {
                ++deleteHits;
                const QByteArray body =
                    "{\"id\":\"\",\"status\":\"cancelled\",\"object\":\"zimage.job\"}";
                const QByteArray head =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " +
                    QByteArray::number(body.size()) + "\r\n"
                                                       "\r\n";
                s->write(head);
                s->write(body);
                s->disconnectFromHost();
                return;
            }
        }
        ++hits[path];
        Route r;
        if (routes.contains(path))
            r = routes.value(path);

        // Serve the PNG image for the /image endpoint.
        if (path.endsWith("/image") && !imageBytes.isEmpty() && r.body.isEmpty())
        {
            const QByteArray head = "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: image/png\r\n"
                                    "Content-Length: " +
                                    QByteArray::number(imageBytes.size()) + "\r\n"
                                    "Connection: close\r\n\r\n";
            s->write(head);
            s->write(imageBytes);
            s->disconnectFromHost();
            return;
        }

        // Dynamic poll responses: cycle through pollResponses[path].
        if (pollResponses.contains(path) && r.body.isEmpty())
        {
            int idx = hits[path] - 1; // 0-based
            if (idx >= 0 && idx < pollResponses[path].size())
                r.body = pollResponses[path][idx];
        }

        const bool small = !r.body.isEmpty();
        QByteArray head = "HTTP/1.1 " + QByteArray::number(r.status) + " " + r.reason + "\r\n"
                          "Connection: close\r\n";
        if (!r.contentType.isEmpty())
            head += "Content-Type: " + r.contentType + "\r\n";
        if (small)
            head += "Content-Length: " + QByteArray::number(r.body.size()) + "\r\n";
        head += "\r\n";
        s->write(head);
        if (small)
            s->write(r.body);
        s->flush();
        s->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buf;
};

// Simple 1x1 red PNG (89 bytes, valid PNG structure).
QByteArray makeOnePixelPng()
{
    static const uchar data[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
        0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x18, 0xDD, 0x8D, 0x4B, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    return QByteArray(reinterpret_cast<const char *>(data), sizeof(data));
}

// Wait for a predicate to become true.
template <class Pred>
bool waitForSignal(Pred pred, int ms = 15000)
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

} // namespace

class TestImageGen : public QObject
{
    Q_OBJECT

private slots:
    // -- static helpers --------------------------------------------------------

    void presetMapping()
    {
        // scene + draft -> zimage-scene-draft.
        QCOMPARE(ImageGen::presetFor(QStringLiteral("scene"), QStringLiteral("draft")),
                 QStringLiteral("zimage-scene-draft"));
        // scene + normal -> zimage-scene.
        QCOMPARE(ImageGen::presetFor(QStringLiteral("scene"), QStringLiteral("normal")),
                 QStringLiteral("zimage-scene"));
        // people + draft -> zimage-people-quick.
        QCOMPARE(ImageGen::presetFor(QStringLiteral("people"), QStringLiteral("draft")),
                 QStringLiteral("zimage-people-quick"));
        // people + normal -> zimage-people.
        QCOMPARE(ImageGen::presetFor(QStringLiteral("people"), QStringLiteral("normal")),
                 QStringLiteral("zimage-people"));
        // any + max -> zimage-max.
        QCOMPARE(ImageGen::presetFor(QStringLiteral("scene"), QStringLiteral("max")),
                 QStringLiteral("zimage-max"));
        QCOMPARE(ImageGen::presetFor(QStringLiteral("people"), QStringLiteral("max")),
                 QStringLiteral("zimage-max"));
    }

    void aspectMapping()
    {
        QCOMPARE(ImageGen::aspectFromSize(QStringLiteral("square")),
                 QStringLiteral("1024x1024"));
        QCOMPARE(ImageGen::aspectFromSize(QStringLiteral("landscape")),
                 QStringLiteral("1024x576"));
        QCOMPARE(ImageGen::aspectFromSize(QStringLiteral("portrait")),
                 QStringLiteral("576x1024"));
        // Unknown falls back to square.
        QCOMPARE(ImageGen::aspectFromSize(QStringLiteral("widescreen")),
                 QStringLiteral("1024x1024"));
    }

    void toolDefinitionHasCorrectName()
    {
        const QJsonObject def = ImageGen::toolDefinition();
        QCOMPARE(def.value(QStringLiteral("type")).toString(), QStringLiteral("function"));
        QCOMPARE(def.value(QStringLiteral("function")).toObject()
                     .value(QStringLiteral("name"))
                     .toString(),
                 QStringLiteral("generate_image"));
        const QJsonObject params = def.value(QStringLiteral("function")).toObject()
                                       .value(QStringLiteral("parameters"))
                                       .toObject();
        QCOMPARE(params.value(QStringLiteral("required")).toArray().first().toString(),
                 QStringLiteral("prompt"));
    }

    void toolDefinitionRequiresPrompt()
    {
        const QJsonObject def = ImageGen::toolDefinition();
        const QJsonObject props = def.value(QStringLiteral("function")).toObject()
                                      .value(QStringLiteral("parameters"))
                                      .toObject()
                                      .value(QStringLiteral("properties"))
                                      .toObject();
        QVERIFY(props.contains(QStringLiteral("prompt")));
        QVERIFY(props.value(QStringLiteral("prompt")).toObject()
                    .value(QStringLiteral("type"))
                    .toString() == QStringLiteral("string"));
    }

    void progressTextQueued()
    {
        const QString s = ImageGen::formatProgressText(QStringLiteral("queued"), 0, {}, 3, 180);
        QVERIFY(s.contains(QStringLiteral("Waiting")));
        QVERIFY(s.contains(QStringLiteral("3 ahead")));
        QVERIFY(s.contains(QStringLiteral("3 min"))); // (180+30)/60 = 3
    }

    void progressTextRunning()
    {
        const QString s =
            ImageGen::formatProgressText(QStringLiteral("running"), 45.5, QStringLiteral("sampling"),
                                         0, 40);
        // The whole line, so a doubled "%%" (QString::arg has no escape) cannot slip by.
        QCOMPARE(s, QStringLiteral("Drawing... 45% (sampling), about 40 s left"));
    }

    void toolResultText()
    {
        const QString r = ImageGen::formatToolResult(QStringLiteral("j_1234"),
                                                     QStringLiteral("zimage-scene"), 2048, 2048, 42);
        QVERIFY(r.contains(QStringLiteral("job_id: j_1234")));
        QVERIFY(r.contains(QStringLiteral("preset: zimage-scene")));
        QVERIFY(r.contains(QStringLiteral("size: 2048x2048")));
        QVERIFY(r.contains(QStringLiteral("seed: 42")));
        QVERIFY(r.contains(QStringLiteral("You have not seen the image.")));
    }

    void errorBusy()
    {
        const QJsonObject body{
            {QStringLiteral("code"), QStringLiteral("busy")},
            {QStringLiteral("message"), QStringLiteral("queue full")},
        };
        const QString s = ImageGen::mapError(body);
        QVERIFY(s.contains(QStringLiteral("busy")));
        QVERIFY(s.contains(QStringLiteral("one image rendering")));
        QVERIFY(s.contains(QStringLiteral("6 waiting")));
    }

    void errorBackend()
    {
        const QJsonObject body{
            {QStringLiteral("code"), QStringLiteral("backend_error")},
            {QStringLiteral("message"), QStringLiteral("CUDA out of memory")},
        };
        const QString s = ImageGen::mapError(body);
        QCOMPARE(s, QStringLiteral("CUDA out of memory"));
    }

    void errorFallback()
    {
        const QJsonObject body{
            {QStringLiteral("code"), QStringLiteral("unknown_code")},
            {QStringLiteral("message"), QStringLiteral("weird error")},
        };
        QCOMPARE(ImageGen::mapError(body), QStringLiteral("weird error"));
    }

    void errorNoMessage()
    {
        const QJsonObject body{{QStringLiteral("code"), QStringLiteral("unknown_code")}};
        QCOMPARE(ImageGen::mapError(body), QStringLiteral("Image generation failed."));
    }

    void imageForToolResultRoundTrip()
    {
        // Create a temp dir with a valid image file + json + tool-content.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString imagesDir = dir.path() + QStringLiteral("/images");
        QDir().mkpath(imagesDir);

        const QString jobId = QStringLiteral("j_12345678_000005_abcd");
        const QByteArray png = makeOnePixelPng();
        const QString pngPath = imagesDir + QStringLiteral("/") + jobId + QStringLiteral(".png");
        {
            QFile f(pngPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(png);
            f.close();
        }

        const QJsonObject meta{
            {QStringLiteral("job_id"), jobId},
            {QStringLiteral("prompt"), QStringLiteral("a test")},
            {QStringLiteral("preset"), QStringLiteral("zimage-scene")},
            {QStringLiteral("seed"), 42},
            {QStringLiteral("width"), 2048},
            {QStringLiteral("height"), 2048},
        };
        const QString jsonPath = pngPath.left(pngPath.size() - 4) + QStringLiteral(".json");
        {
            QFile jsonFile(jsonPath);
            QVERIFY(jsonFile.open(QIODevice::WriteOnly));
            jsonFile.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
            jsonFile.close();
        }

        const QString toolContent =
            ImageGen::formatToolResult(jobId, QStringLiteral("zimage-scene"), 2048, 2048, 42);

        QString foundJobId;
        QString foundPath;
        QJsonObject foundFields;
        QVERIFY(ImageGen::imageForToolResult(toolContent, dir.path(), foundJobId, foundPath,
                                             foundFields));
        QCOMPARE(foundJobId, jobId);
        QCOMPARE(foundPath, pngPath);
        QCOMPARE(foundFields.value(QStringLiteral("job_id")).toString(), jobId);
        QCOMPARE(foundFields.value(QStringLiteral("preset")).toString(),
                 QStringLiteral("zimage-scene"));
        QCOMPARE(foundFields.value(QStringLiteral("width")).toInt(), 2048);

        // Missing files should return false.
        QDir().remove(pngPath);
        QVERIFY(!ImageGen::imageForToolResult(toolContent, dir.path(), foundJobId, foundPath,
                                              foundFields));
    }

    void imageForToolResultNoJobId()
    {
        QString jobId, path;
        QJsonObject fields;
        QVERIFY(!ImageGen::imageForToolResult("no job here", "/tmp", jobId, path, fields));
    }

    // -- full async lifecycle --------------------------------------------------

    void fullSubmitPollDownloadFlow()
    {
        // Submit -> poll (queued -> running -> succeeded) -> download -> files.
        TestServer srv;
        QVERIFY(srv.listen());

        const QByteArray png = makeOnePixelPng();
        srv.imageBytes = png;

        // Submission: 202 with job object.
        const QString jobId = QStringLiteral("j_12345678_000001_abcd");
        {
            QJsonObject jobObj{
                {QStringLiteral("id"), jobId},
                {QStringLiteral("object"), QStringLiteral("zimage.job")},
                {QStringLiteral("kind"), QStringLiteral("generate")},
                {QStringLiteral("status"), QStringLiteral("queued")},
                {QStringLiteral("model"), QStringLiteral("zimage-scene")},
                {QStringLiteral("aspect"), QStringLiteral("square")},
                {QStringLiteral("queue_position"), 0},
                {QStringLiteral("estimate_s"), 0},
            };
            srv.routes[QStringLiteral("/v1/images/generations")].status = 202;
            srv.routes[QStringLiteral("/v1/images/generations")].body =
                QJsonDocument(jobObj).toJson(QJsonDocument::Compact);
        }

        // Poll responses: queued, running, succeeded.
        const QString pollPath = QStringLiteral("/v1/jobs/") + jobId;
        srv.pollResponses[pollPath] = {
            // 1st poll: queued.
            QJsonDocument{
                QJsonObject{
                    {QStringLiteral("status"), QStringLiteral("queued")},
                    {QStringLiteral("queue_position"), 2},
                    {QStringLiteral("estimate_s"), 300},
                    {QStringLiteral("progress"),
                     QJsonObject{{"stage", "encode"}, {"percent", 0.0}}}}}
                .toJson(QJsonDocument::Compact),
            // 2nd poll: running.
            QJsonDocument{
                QJsonObject{
                    {QStringLiteral("status"), QStringLiteral("running")},
                    {QStringLiteral("queue_position"), 0},
                    // Fractional, as zimage-gw sends it (round1), not a whole number.
                    {QStringLiteral("estimate_s"), 45.3},
                    {QStringLiteral("progress"),
                     QJsonObject{{"stage", "sampling"}, {"percent", 35.0}}}}}
                .toJson(QJsonDocument::Compact),
            // 3rd poll: succeeded.
            QJsonDocument{
                QJsonObject{
                    {QStringLiteral("status"), QStringLiteral("succeeded")},
                    {QStringLiteral("queue_position"), 0},
                    {QStringLiteral("estimate_s"), 0},
                    {QStringLiteral("result"),
                     QJsonObject{
                         {QStringLiteral("width"), 2048},
                         {QStringLiteral("height"), 2048},
                         {QStringLiteral("seed"), 42},
                         {QStringLiteral("preset"), QStringLiteral("zimage-scene")}}}}}
                .toJson(QJsonDocument::Compact)};

        ImageGen gen;
        QSignalSpy finishedSpy(&gen, &ImageGen::finished);
        QSignalSpy failedSpy(&gen, &ImageGen::failed);
        QSignalSpy progressSpy(&gen, &ImageGen::progress);

        QTemporaryDir dataDir;
        ImageGen::ImageJobSpec spec;
        spec.baseUrl = QUrl(srv.url(QStringLiteral("/")));
        spec.token = QStringLiteral("test-token");
        spec.preset = QStringLiteral("zimage-scene");
        spec.prompt = QStringLiteral("a beautiful landscape");
        spec.aspect = QStringLiteral("square");
        spec.seed = 42;
        spec.dataDirectory = dataDir.path();

        gen.start(spec);

        // Wait for the job to finish (submit + 3 polls + download + save).
        QVERIFY(waitForSignal([&]() { return finishedSpy.size() > 0; }, 20000));
        // Fix 5: exactly one end signal per start().
        QCOMPARE(finishedSpy.size(), 1);
        QCOMPARE(failedSpy.size(), 0);
        const QString savedPath = finishedSpy[0][1].toString();
        QVERIFY(savedPath.endsWith(jobId + QStringLiteral(".png")));
        QVERIFY(QFile::exists(savedPath));

        // Check that progress was emitted during polling.
        QVERIFY(progressSpy.size() >= 2); // at least queued + running progress
        // The running poll shows the server's estimate, not 0 (toInt() gives 0 for 45.3).
        bool sawRunning = false;
        for (const QList<QVariant> &args : std::as_const(progressSpy))
        {
            const QString text = args.value(1).toString();
            if (text.startsWith(QStringLiteral("Drawing")))
            {
                QCOMPARE(text, QStringLiteral("Drawing... 35% (sampling), about 45 s left"));
                sawRunning = true;
            }
        }
        QVERIFY(sawRunning);

        // Verify JSON metadata was written with server-provided width/height/seed.
        const QString jsonPath = savedPath.left(savedPath.size() - 4) + QStringLiteral(".json");
        QVERIFY(QFile::exists(jsonPath));
        {
            QFile jsonFile(jsonPath);
            QVERIFY(jsonFile.open(QIODevice::ReadOnly));
            QJsonParseError parseErr;
            const QJsonObject meta =
                QJsonDocument::fromJson(jsonFile.readAll(), &parseErr).object();
            QCOMPARE(meta.value(QStringLiteral("job_id")).toString(), jobId);
            QCOMPARE(meta.value(QStringLiteral("preset")).toString(),
                     QStringLiteral("zimage-scene"));
            QCOMPARE(meta.value(QStringLiteral("width")).toInt(), 2048);
            QCOMPARE(meta.value(QStringLiteral("height")).toInt(), 2048);
            QCOMPARE(meta.value(QStringLiteral("seed")).toInt(), 42);
        }

        // Verify the PNG was actually saved.
        {
            QFile pngFile(savedPath);
            QVERIFY(pngFile.open(QIODevice::ReadOnly));
            const QByteArray savedPng = pngFile.readAll();
            QCOMPARE(savedPng, png);
        }

        // Verify that imageForToolResult finds the files.
        {
            QString foundJobId;
            QString foundPath;
            QJsonObject foundFields;
            const QString toolContent =
                ImageGen::formatToolResult(jobId, QStringLiteral("zimage-scene"), 2048, 2048, 42);
            QVERIFY(ImageGen::imageForToolResult(toolContent, spec.dataDirectory, foundJobId,
                                                 foundPath, foundFields));
            QCOMPARE(foundJobId, jobId);
            QCOMPARE(foundPath, savedPath);
        }
    }

    void busyErrorOnSubmit()
    {
        // Submit returns 429 busy -> immediate failure.
        TestServer srv;
        QVERIFY(srv.listen());

        QJsonObject errBody{
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), QStringLiteral("busy")},
                         {QStringLiteral("message"), QStringLiteral("queue full")}}},
        };
        srv.routes[QStringLiteral("/v1/images/generations")].status = 429;
        srv.routes[QStringLiteral("/v1/images/generations")].body =
            QJsonDocument(errBody).toJson(QJsonDocument::Compact);

        ImageGen gen;
        QSignalSpy failedSpy(&gen, &ImageGen::failed);

        QTemporaryDir dataDir;
        ImageGen::ImageJobSpec spec;
        spec.baseUrl = QUrl(srv.url(QStringLiteral("/")));
        spec.token = QStringLiteral("test-token");
        spec.preset = QStringLiteral("zimage-scene");
        spec.prompt = QStringLiteral("test prompt");
        spec.dataDirectory = dataDir.path();

        gen.start(spec);

        QVERIFY(waitForSignal([&]() { return failedSpy.size() > 0; }, 5000));
        QCOMPARE(failedSpy.size(), 1);
        QVERIFY(failedSpy[0][1].toString().contains(QStringLiteral("busy")));
    }

    void jobFailedOnServer()
    {
        // Submit succeeds, poll returns failed.
        TestServer srv;
        QVERIFY(srv.listen());

        const QString jobId = QStringLiteral("j_23456789_000002_ef01");
        {
            QJsonObject jobObj{
                {QStringLiteral("id"), jobId},
                {QStringLiteral("object"), QStringLiteral("zimage.job")},
                {QStringLiteral("kind"), QStringLiteral("generate")},
                {QStringLiteral("status"), QStringLiteral("queued")},
                {QStringLiteral("model"), QStringLiteral("zimage-scene")},
                {QStringLiteral("aspect"), QStringLiteral("square")},
                {QStringLiteral("queue_position"), 0},
                {QStringLiteral("estimate_s"), 0},
            };
            srv.routes[QStringLiteral("/v1/images/generations")].status = 202;
            srv.routes[QStringLiteral("/v1/images/generations")].body =
                QJsonDocument(jobObj).toJson(QJsonDocument::Compact);
        }

        // Poll returns failed.
        QJsonObject failJobObj{
            {QStringLiteral("status"), QStringLiteral("failed")},
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), QStringLiteral("backend_error")},
                         {QStringLiteral("message"), QStringLiteral("CUDA error")}}},
        };
        const QString pollPath = QStringLiteral("/v1/jobs/") + jobId;
        srv.pollResponses[pollPath] = {
            QJsonDocument(failJobObj).toJson(QJsonDocument::Compact)};

        ImageGen gen;
        QSignalSpy failedSpy(&gen, &ImageGen::failed);
        QSignalSpy finishedSpy(&gen, &ImageGen::finished);

        QTemporaryDir dataDir;
        ImageGen::ImageJobSpec spec;
        spec.baseUrl = QUrl(srv.url(QStringLiteral("/")));
        spec.token = QStringLiteral("test-token");
        spec.preset = QStringLiteral("zimage-scene");
        spec.prompt = QStringLiteral("test prompt");
        spec.dataDirectory = dataDir.path();

        gen.start(spec);

        QVERIFY(waitForSignal([&]() { return failedSpy.size() > 0; }, 10000));
        QCOMPARE(failedSpy.size(), 1);
        QCOMPARE(finishedSpy.size(), 0);
        QVERIFY(failedSpy[0][1].toString().contains(QStringLiteral("CUDA error")));
    }

    void fiveFailedPollsGivesUp()
    {
        // Polls fail 5 times in a row -> "lost contact" message.
        TestServer srv;
        QVERIFY(srv.listen());

        const QString jobId = QStringLiteral("j_34567890_000003_2345");
        {
            QJsonObject jobObj{
                {QStringLiteral("id"), jobId},
                {QStringLiteral("object"), QStringLiteral("zimage.job")},
                {QStringLiteral("kind"), QStringLiteral("generate")},
                {QStringLiteral("status"), QStringLiteral("queued")},
                {QStringLiteral("model"), QStringLiteral("zimage-scene")},
                {QStringLiteral("aspect"), QStringLiteral("square")},
                {QStringLiteral("queue_position"), 0},
                {QStringLiteral("estimate_s"), 0},
            };
            srv.routes[QStringLiteral("/v1/images/generations")].status = 202;
            srv.routes[QStringLiteral("/v1/images/generations")].body =
                QJsonDocument(jobObj).toJson(QJsonDocument::Compact);
        }

        // All polls return 400 (error) -> consecutiveFailures increments.
        srv.routes[QStringLiteral("/v1/jobs/") + jobId] =
            TestServer::Route{400, "Bad Request", "application/json",
                  QByteArray("{\"error\":{\"code\":\"invalid_request\",\"message\":\"bad\"}}")};

        ImageGen gen;
        QSignalSpy failedSpy(&gen, &ImageGen::failed);

        QTemporaryDir dataDir;
        ImageGen::ImageJobSpec spec;
        spec.baseUrl = QUrl(srv.url(QStringLiteral("/")));
        spec.token = QStringLiteral("test-token");
        spec.preset = QStringLiteral("zimage-scene");
        spec.prompt = QStringLiteral("test prompt");
        spec.dataDirectory = dataDir.path();

        gen.start(spec);

        // After 5 failed polls (5 x ~2s + initial delay) -> give up.
        QVERIFY(waitForSignal([&]() { return failedSpy.size() > 0; }, 30000));
        QCOMPARE(failedSpy.size(), 1);
        QVERIFY(failedSpy[0][1].toString().contains(
            QStringLiteral("lost contact with the image server")));
    }

    void cancelSendsDelete()
    {
        // Submit succeeds, then cancel sends DELETE and emits failed("cancelled").
        TestServer srv;
        QVERIFY(srv.listen());

        const QString jobId = QStringLiteral("j_45678901_000004_6789");
        {
            QJsonObject jobObj{
                {QStringLiteral("id"), jobId},
                {QStringLiteral("object"), QStringLiteral("zimage.job")},
                {QStringLiteral("kind"), QStringLiteral("generate")},
                {QStringLiteral("status"), QStringLiteral("queued")},
                {QStringLiteral("model"), QStringLiteral("zimage-scene")},
                {QStringLiteral("aspect"), QStringLiteral("square")},
                {QStringLiteral("queue_position"), 0},
                {QStringLiteral("estimate_s"), 0},
            };
            srv.routes[QStringLiteral("/v1/images/generations")].status = 202;
            srv.routes[QStringLiteral("/v1/images/generations")].body =
                QJsonDocument(jobObj).toJson(QJsonDocument::Compact);
        }

        // Poll endpoint returns queued so the poll timer doesn't trigger.
        srv.routes[QStringLiteral("/v1/jobs/") + jobId] =
            TestServer::Route{200, "OK", "application/json",
                  QByteArray("{\"status\":\"queued\"}")};

        ImageGen gen;
        QSignalSpy failedSpy(&gen, &ImageGen::failed);

        QTemporaryDir dataDir;
        ImageGen::ImageJobSpec spec;
        spec.baseUrl = QUrl(srv.url(QStringLiteral("/")));
        spec.token = QStringLiteral("test-token");
        spec.preset = QStringLiteral("zimage-scene");
        spec.prompt = QStringLiteral("test prompt");
        spec.dataDirectory = dataDir.path();

        gen.start(spec);

        // Wait for submit to complete (2s poll timer + network).
        QTest::qWait(3000);
        QCOMPARE(failedSpy.size(), 0); // not failed yet

        // Cancel the job.
        gen.cancel();

        // Should emit failed with "cancelled".
        QVERIFY(waitForSignal([&]() { return failedSpy.size() > 0; }, 5000));
        QCOMPARE(failedSpy.size(), 1);
        QCOMPARE(failedSpy[0][1].toString(), QStringLiteral("cancelled"));

        // Give the async DELETE request time to reach the server.
        QTest::qWait(500);
        QVERIFY(srv.deleteHits > 0);
    }

    void cancelDuringSubmit()
    {
        // Cancel before submit responds -> abort the submit request, emit
        // failed(empty, "cancelled"), no other end signals.
        TestServer srv;
        QVERIFY(srv.listen());

        // Submission: do not respond (no route set) -> the request hangs.
        // We just need the server to accept the connection so the POST is in-flight.

        ImageGen gen;
        QSignalSpy failedSpy(&gen, &ImageGen::failed);

        QTemporaryDir dataDir;
        ImageGen::ImageJobSpec spec;
        spec.baseUrl = QUrl(srv.url(QStringLiteral("/")));
        spec.token = QStringLiteral("test-token");
        spec.preset = QStringLiteral("zimage-scene");
        spec.prompt = QStringLiteral("test prompt");
        spec.dataDirectory = dataDir.path();

        gen.start(spec);

        // Cancel almost immediately, before the server responds.
        gen.cancel();

        // Should emit exactly one failed with empty id and "cancelled".
        QVERIFY(waitForSignal([&]() { return failedSpy.size() > 0; }, 5000));
        QCOMPARE(failedSpy.size(), 1);
        QCOMPARE(failedSpy[0][0].toString(), QString{});
        QCOMPARE(failedSpy[0][1].toString(), QStringLiteral("cancelled"));
    }

    // -- test connection -------------------------------------------------------

    void testConnectionOk()
    {
        TestServer srv;
        QVERIFY(srv.listen());

        QJsonObject health{
            {QStringLiteral("status"), QStringLiteral("ok")},
            {QStringLiteral("version"), QStringLiteral("phase-2")},
        };
        srv.routes[QStringLiteral("/v1/health")].body =
            QJsonDocument(health).toJson(QJsonDocument::Compact);

        ImageGen gen;
        QSignalSpy testedSpy(&gen, &ImageGen::connectionTested);

        gen.testConnection(QUrl(srv.url(QStringLiteral("/"))));

        QVERIFY(waitForSignal([&]() { return testedSpy.size() > 0; }, 5000));
        QCOMPARE(testedSpy.size(), 1);
        QCOMPARE(testedSpy[0][0].toBool(), true);
        QCOMPARE(testedSpy[0][1].toString(), QStringLiteral("ok"));
    }

    void testConnectionFails()
    {
        TestServer srv;
        QVERIFY(srv.listen());

        srv.routes[QStringLiteral("/v1/health")].status = 503;
        srv.routes[QStringLiteral("/v1/health")].body =
            QByteArray("{\"error\":{\"code\":\"low_memory\",\"message\":\"not enough RAM\"}}");

        ImageGen gen;
        QSignalSpy testedSpy(&gen, &ImageGen::connectionTested);

        gen.testConnection(QUrl(srv.url(QStringLiteral("/"))));

        QVERIFY(waitForSignal([&]() { return testedSpy.size() > 0; }, 5000));
        QCOMPARE(testedSpy.size(), 1);
        QCOMPARE(testedSpy[0][0].toBool(), false);
    }

    // -- Step 11 fixes ---------------------------------------------------------

    void submitBodyContainsSize()
    {
        // The submit POST body must include the size field so the server
        // renders the correct aspect.
        TestServer srv;
        QVERIFY(srv.listen());

        const QString jobId = QStringLiteral("j_11111111_000003_aaaa");
        {
            QJsonObject jobObj{
                {QStringLiteral("id"), jobId},
                {QStringLiteral("object"), QStringLiteral("zimage.job")},
                {QStringLiteral("kind"), QStringLiteral("generate")},
                {QStringLiteral("status"), QStringLiteral("queued")},
                {QStringLiteral("model"), QStringLiteral("zimage-scene")},
                {QStringLiteral("aspect"), QStringLiteral("landscape")},
                {QStringLiteral("queue_position"), 0},
                {QStringLiteral("estimate_s"), 0},
            };
            srv.routes[QStringLiteral("/v1/images/generations")].status = 202;
            srv.routes[QStringLiteral("/v1/images/generations")].body =
                QJsonDocument(jobObj).toJson(QJsonDocument::Compact);
        }

        const QString pollPath = QStringLiteral("/v1/jobs/") + jobId;
        srv.pollResponses[pollPath] = {
            QJsonDocument{QJsonObject{
                {QStringLiteral("status"), QStringLiteral("succeeded")},
                {QStringLiteral("queue_position"), 0},
                {QStringLiteral("estimate_s"), 0},
                {QStringLiteral("result"),
                 QJsonObject{
                     {QStringLiteral("width"), 1024},
                     {QStringLiteral("height"), 576},
                     {QStringLiteral("seed"), 7}}}}}.toJson(QJsonDocument::Compact)};
        srv.imageBytes = makeOnePixelPng();

        ImageGen gen;
        QSignalSpy finishedSpy(&gen, &ImageGen::finished);

        QTemporaryDir dataDir;
        ImageGen::ImageJobSpec spec;
        spec.baseUrl = QUrl(srv.url(QStringLiteral("/")));
        spec.token = QStringLiteral("test-token");
        spec.preset = QStringLiteral("zimage-scene");
        spec.prompt = QStringLiteral("a landscape");
        spec.aspect = QStringLiteral("1024x576");
        spec.dataDirectory = dataDir.path();

        gen.start(spec);

        QVERIFY(waitForSignal([&]() { return finishedSpy.size() > 0; }, 10000));
        QCOMPARE(finishedSpy.size(), 1);

        // Parse the captured submit body and verify the size field.
        QJsonParseError parseErr;
        const QJsonObject body =
            QJsonDocument::fromJson(srv.lastSubmitBody, &parseErr).object();
        QCOMPARE(parseErr.error, QJsonParseError::NoError);
        QCOMPARE(body.value(QStringLiteral("size")).toString(),
                 QStringLiteral("1024x576"));
        QCOMPARE(body.value(QStringLiteral("model")).toString(),
                 QStringLiteral("zimage-scene"));
        QVERIFY(body.value(QStringLiteral("async")).toBool());
    }

    void unknownPollStatusFails()
    {
        // An unexpected status from the server must fail immediately, not
        // hang in an infinite poll loop.
        TestServer srv;
        QVERIFY(srv.listen());

        const QString jobId = QStringLiteral("j_33333333_000004_bbbb");
        {
            QJsonObject jobObj{
                {QStringLiteral("id"), jobId},
                {QStringLiteral("object"), QStringLiteral("zimage.job")},
                {QStringLiteral("kind"), QStringLiteral("generate")},
                {QStringLiteral("status"), QStringLiteral("queued")},
                {QStringLiteral("model"), QStringLiteral("zimage-scene")},
                {QStringLiteral("aspect"), QStringLiteral("square")},
                {QStringLiteral("queue_position"), 0},
                {QStringLiteral("estimate_s"), 0},
            };
            srv.routes[QStringLiteral("/v1/images/generations")].status = 202;
            srv.routes[QStringLiteral("/v1/images/generations")].body =
                QJsonDocument(jobObj).toJson(QJsonDocument::Compact);
        }

        // Poll returns an unexpected status.
        QJsonObject unknownStatus{
            {QStringLiteral("status"), QStringLiteral("refining")},
            {QStringLiteral("progress"), QJsonObject{{"stage", ""}, {"percent", 0.0}}},
        };
        const QString pollPath = QStringLiteral("/v1/jobs/") + jobId;
        srv.pollResponses[pollPath] = {
            QJsonDocument(unknownStatus).toJson(QJsonDocument::Compact)};

        ImageGen gen;
        QSignalSpy failedSpy(&gen, &ImageGen::failed);
        QSignalSpy finishedSpy(&gen, &ImageGen::finished);

        QTemporaryDir dataDir;
        ImageGen::ImageJobSpec spec;
        spec.baseUrl = QUrl(srv.url(QStringLiteral("/")));
        spec.token = QStringLiteral("test-token");
        spec.preset = QStringLiteral("zimage-scene");
        spec.prompt = QStringLiteral("test");
        spec.dataDirectory = dataDir.path();

        gen.start(spec);

        QVERIFY(waitForSignal([&]() { return failedSpy.size() > 0; }, 10000));
        QCOMPARE(failedSpy.size(), 1);
        QCOMPARE(finishedSpy.size(), 0);
        QVERIFY(failedSpy[0][1].toString().contains("unexpected job status"));
        QVERIFY(failedSpy[0][1].toString().contains("refining"));
    }

    void submitMissingIdFailsImmediately()
    {
        // A 202 without an id field must fail immediately, not poll for ~12 s.
        TestServer srv;
        QVERIFY(srv.listen());

        // Submit returns 202 with no id.
        QJsonObject submitResp{
            {QStringLiteral("object"), QStringLiteral("zimage.job")},
            {QStringLiteral("status"), QStringLiteral("queued")},
        };
        srv.routes[QStringLiteral("/v1/images/generations")].status = 202;
        srv.routes[QStringLiteral("/v1/images/generations")].body =
            QJsonDocument(submitResp).toJson(QJsonDocument::Compact);

        ImageGen gen;
        QSignalSpy failedSpy(&gen, &ImageGen::failed);
        QSignalSpy finishedSpy(&gen, &ImageGen::finished);

        QTemporaryDir dataDir;
        ImageGen::ImageJobSpec spec;
        spec.baseUrl = QUrl(srv.url(QStringLiteral("/")));
        spec.token = QStringLiteral("test-token");
        spec.preset = QStringLiteral("zimage-scene");
        spec.prompt = QStringLiteral("test");
        spec.dataDirectory = dataDir.path();

        gen.start(spec);

        // Should fail quickly, not after ~12 s of polling.
        QVERIFY(waitForSignal([&]() { return failedSpy.size() > 0; }, 5000));
        QCOMPARE(failedSpy.size(), 1);
        QCOMPARE(finishedSpy.size(), 0);
        QVERIFY(failedSpy[0][1].toString().contains("did not return a job id"));
        QCOMPARE(failedSpy[0][0].toString(), QString{});
    }
};

QTEST_GUILESS_MAIN(TestImageGen)
#include "test_image_gen.moc"
