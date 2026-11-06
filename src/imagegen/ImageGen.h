#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>

// Async image generation client for the zimage-gw server.
// Manages the full job lifecycle: submit -> poll -> download -> save PNG + JSON.
class ImageGen : public QObject
{
    Q_OBJECT
public:
    explicit ImageGen(QObject *parent = nullptr);

    struct ImageJobSpec
    {
        QUrl baseUrl;
        QString token;
        QString preset;
        QString prompt;
        QString aspect;
        qint64 seed = -1;
        QString dataDirectory;
    };

    // File name offered by the Save as dialog: shammy-YYYY-MM-DD-HHMMSS.png.
    static QString suggestedSaveName(const QDateTime &when);

    // Map subject + quality to the server preset id. "draft" is the same
    // 10-step preset as "normal".
    static QString presetFor(const QString &subject, const QString &quality);

    // The POST /v1/images/generations body. No preset asks the server to upscale.
    static QJsonObject submitBody(const QString &preset, const QString &prompt,
                                  const QString &aspect, qint64 seed);

    // Map aspect keyword to WxH size string.
    static QString aspectFromSize(const QString &aspect);

    // Tool definition JSON for the generate_image tool.
    static QJsonObject toolDefinition();

    // Format progress text for the UI, based on the job status and metrics.
    static QString formatProgressText(const QString &status,
                                      double percent,
                                      const QString &stage,
                                      int queuePosition,
                                      int estimateS);

    // Format the tool result text handed back to the chat model.
    static QString formatToolResult(const QString &jobId,
                                    const QString &preset,
                                    int width,
                                    int height,
                                    qint64 seed);

    // Map a server error JSON body to a user-friendly error string.
    static QString mapError(const QJsonObject &errorBody);

    // Given tool-content text and a data directory, find the original PNG and
    // JSON. An upscale does not replace that image. Returns true if both files
    // exist. The jobId, path and fields are filled in on success.
    static bool imageForToolResult(const QString &toolContent,
                                   const QString &dataDirectory,
                                   QString &jobId,
                                   QString &path,
                                   QJsonObject &fields);

    // One saved upscale shown under its original. scale is 2 or 4.
    struct UpscaleImage
    {
        QString jobId;
        QString path;
        QString prompt;
        int scale = 0;
        int width = 0;
        int height = 0;
    };

    // Caption for an upscale row, e.g. "Upscaled 2× — 2048×2048".
    static QString upscaleLabel(int scale, int width, int height);

    // Upscales of parentId, scale 2 then scale 4. A child's sidecar names the
    // parent. An older replaced_by pointer is included when the child omitted
    // parent. The original sidecar is not modified.
    static QList<UpscaleImage> upscalesFor(const QString &dataDirectory,
                                           const QString &parentId,
                                           const QJsonObject &parentFields);

    // True when the original may still offer this scale. A child of that
    // scale hides the button. An upscale row, and an older scene or people
    // file with no upscaled field, offers neither.
    static bool offersScale(const QJsonObject &fields,
                            const QList<UpscaleImage> &upscales,
                            int scale);

    // Start the full job lifecycle: submit, poll, download, save.
    void start(const ImageJobSpec &spec);

    struct UpscaleJobSpec
    {
        QUrl baseUrl;
        QString token;
        QString dataDirectory;
        // The finished image to upscale. Its PNG and JSON stay on disk.
        QString parentId;
        int scale = 0;
    };

    // POST /v1/jobs/{id}/upscale body. async so the job is polled like a generation.
    static QJsonObject upscaleBody(int scale);

    // True when this image may still be upscaled. An explicit upscaled field
    // wins. Older scene and people sidecars omit it and were already upscaled.
    static bool offersUpscale(const QJsonObject &fields);

    // Upscale a finished image by 2 or 4. Same poll, download and save path as start().
    void upscale(const UpscaleJobSpec &spec);

    // Cancel the currently running job (sends DELETE).
    void cancel();

    // Test the connection (GET /v1/health). No token needed.
    void testConnection(const QUrl &baseUrl);

signals:
    void progress(const QString &jobId, const QString &text);
    void finished(const QString &jobId, const QString &path);
    void failed(const QString &jobId, const QString &error);
    void connectionTested(bool ok, const QString &message);

private:
    // Helper: remove trailing slash from a URL string.
    static QString trimTrailingSlash(const QString &url);

    // State for an active job. QObject-less because we manage it manually.
    struct Job
    {
        QString id;
        QUrl baseUrl;
        QString token;
        QString preset;
        QString aspect;
        QString dataDirectory;
        QString prompt;
        qint64 seed = -1;
        int width = 0;
        int height = 0;
        // Set when this job is an upscale of a finished image. scale is 2 or 4.
        bool upscale = false;
        int scale = 0;
        QString parentId;
        QTimer pollTimer;
        bool cancelled = false;
        int consecutiveFailures = 0;
        QPointer<QNetworkReply> reply;
    };

    // Older scene and people sidecars omit upscaled and were already upscaled.
    static bool presetAlreadyUpscaled(const QString &preset);
    static bool readJsonFile(const QString &path, QJsonObject &fields);
    // Fill one upscale row when its PNG is on disk. False when the PNG is missing.
    static bool readUpscale(const QString &dir, const QString &jobId,
                            const QJsonObject &fields, UpscaleImage &image);

    void submit(Job *job);
    void postJob(Job *job, const QString &path, const QJsonObject &body);
    void startPolling(Job *job);
    void doPoll(Job *job);
    void handlePollResponse(Job *job, QNetworkReply *reply);
    void handlePollError(Job *job, const QString &error);
    void downloadImage(Job *job);
    void saveFiles(Job *job, const QByteArray &imageData, int width, int height);
    bool writeAtomic(const QString &path, const QByteArray &data);
    void sendDelete(Job *job);
    void cleanupJob(Job *job);

    QNetworkAccessManager m_nam;
    QList<Job *> m_jobs;
    Job *m_currentJob = nullptr;
};
