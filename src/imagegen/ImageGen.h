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

    // Map subject + quality to the server preset id.
    static QString presetFor(const QString &subject, const QString &quality);

    // The POST /v1/images/generations body. zimage-people asks for the x4 upscale
    // (Lanczos to 2048), so every normal-quality image is 2048 on the long side.
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

    // Given tool-content text and a data directory, find the PNG + JSON files
    // that belong to the same job. Returns true if both files exist.
    // The jobId, path and fields are filled in on success.
    static bool imageForToolResult(const QString &toolContent,
                                   const QString &dataDirectory,
                                   QString &jobId,
                                   QString &path,
                                   QJsonObject &fields);

    // Start the full job lifecycle: submit, poll, download, save.
    void start(const ImageJobSpec &spec);

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
        QTimer pollTimer;
        bool cancelled = false;
        int consecutiveFailures = 0;
        QPointer<QNetworkReply> reply;
    };

    void submit(Job *job);
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
