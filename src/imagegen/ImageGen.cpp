#include "imagegen/ImageGen.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>

namespace
{

// Regex to extract the job ID from tool result text.
// Matches "job_id: j_XXXXXXXX_XXXX_YYYY" where X=digits, Y=hex.
const QRegularExpression kJobIdRegex(QStringLiteral("job_id:\\s*(j_\\d{8}_\\d{6}_[0-9a-f]{4})"));

} // namespace

ImageGen::ImageGen(QObject *parent)
    : QObject(parent)
{
}

QString ImageGen::trimTrailingSlash(const QString &url)
{
    // Remove trailing slash from URL.
    QString result = url;
    while (result.size() > 1 && result.endsWith('/'))
    {
        result.chop(1);
    }
    return result;
}

QString ImageGen::suggestedSaveName(const QDateTime &when)
{
    // A dated name, so the dialog starts on a file and not a bare folder.
    return QStringLiteral("shammy-%1.png").arg(when.toString(QStringLiteral("yyyy-MM-dd-HHmmss")));
}

QString ImageGen::presetFor(const QString &subject, const QString &quality)
{
    // Map subject + quality to the zimage-gw preset. "normal" and "draft"
    // both use the 10-step floor. Anything else, including no quality, is max.
    const bool people = subject == QLatin1String("people");
    if (quality == QLatin1String("normal") || quality == QLatin1String("draft"))
    {
        return people ? QStringLiteral("zimage-people") : QStringLiteral("zimage-scene");
    }
    return QStringLiteral("zimage-max");
}

QString ImageGen::aspectFromSize(const QString &aspect)
{
    // Map aspect keyword to WxH size string.
    if (aspect == QStringLiteral("square"))
    {
        return QStringLiteral("1024x1024");
    }
    if (aspect == QStringLiteral("landscape"))
    {
        return QStringLiteral("1024x576");
    }
    if (aspect == QStringLiteral("portrait"))
    {
        return QStringLiteral("576x1024");
    }
    return QStringLiteral("1024x1024"); // default
}

QJsonObject ImageGen::toolDefinition()
{
    // Define the generate_image tool for the chat model.
    QJsonObject params;
    params.insert(QStringLiteral("type"), QStringLiteral("object"));
    QJsonObject properties;
    QJsonObject promptProp;
    promptProp.insert(QStringLiteral("type"), QStringLiteral("string"));
    promptProp.insert(QStringLiteral("description"),
                      QStringLiteral("The image prompt. Describe the image in detail."));
    properties.insert(QStringLiteral("prompt"), promptProp);

    QJsonObject subjectProp;
    subjectProp.insert(QStringLiteral("type"), QStringLiteral("string"));
    subjectProp.insert(
        QStringLiteral("description"),
        QStringLiteral("Scene (places and things) or people. Use 'people' for portraits of "
                       "people."));
    properties.insert(QStringLiteral("subject"), subjectProp);

    QJsonObject aspectProp;
    aspectProp.insert(QStringLiteral("type"), QStringLiteral("string"));
    aspectProp.insert(
        QStringLiteral("description"),
        QStringLiteral("Aspect ratio: square (1:1), landscape (16:9), or portrait (9:16)."));
    aspectProp.insert(QStringLiteral("default"), QStringLiteral("square"));
    properties.insert(QStringLiteral("aspect"), aspectProp);

    QJsonObject qualityProp;
    qualityProp.insert(QStringLiteral("type"), QStringLiteral("string"));
    // The tool offers max or normal. The words that used to mean a draft
    // now ask for normal, which is the 10-step 768 floor.
    qualityProp.insert(QStringLiteral("enum"),
                       QJsonArray{QStringLiteral("max"), QStringLiteral("normal")});
    qualityProp.insert(
        QStringLiteral("description"),
        QStringLiteral("max (the default): the most detail the model can draw, 1344 pixels on "
                       "the long side (1024 square), in about 12 minutes. Use max unless the "
                       "user asks for something faster. normal: when the user says "
                       "\"normal quality\", \"quickly\", \"quick\", \"fast\", \"draft\" or "
                       "\"preview\"; 10 steps at 768, about 5 minutes. When the user says "
                       "one of these words, always pass quality normal: leaving it out "
                       "gives max."));
    qualityProp.insert(QStringLiteral("default"), QStringLiteral("max"));
    properties.insert(QStringLiteral("quality"), qualityProp);

    QJsonObject seedProp;
    seedProp.insert(QStringLiteral("type"), QStringLiteral("integer"));
    seedProp.insert(
        QStringLiteral("description"),
        QStringLiteral("Optional seed for reproducibility. Omit for a random seed."));
    properties.insert(QStringLiteral("seed"), seedProp);

    params.insert(QStringLiteral("properties"), properties);
    params.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("prompt")});

    QJsonObject fn;
    fn.insert(QStringLiteral("name"), QStringLiteral("generate_image"));
    fn.insert(
        QStringLiteral("description"),
        QStringLiteral("Generate a picture (photo, painting or illustration) from a text "
                       "prompt. Call this whenever the user asks for an image, photo or picture; "
                       "never draw one as an SVG or HTML artifact instead. Use 'subject: people' "
                       "for portraits of people. Use 'subject: scene' for places and things."));
    fn.insert(QStringLiteral("parameters"), params);

    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"), fn},
    };
}

QString ImageGen::formatProgressText(const QString &status,
                                     double percent,
                                     const QString &stage,
                                     int queuePosition,
                                     int estimateS)
{
    // Format progress text for display in the UI.
    if (status == QStringLiteral("queued"))
    {
        return QStringLiteral("Waiting for the image server: %1 ahead, about %2 min")
            .arg(queuePosition)
            .arg((estimateS + 30) / 60);
    }
    if (status == QStringLiteral("running"))
    {
        return QStringLiteral("Drawing... %1% (%2), about %3 s left")
            .arg(static_cast<int>(percent))
            .arg(stage)
            .arg(estimateS);
    }
    if (status == QStringLiteral("cancelled"))
    {
        return QStringLiteral("Image generation was cancelled");
    }
    if (status == QStringLiteral("succeeded"))
    {
        return QStringLiteral("Image generation complete");
    }
    return QString();
}

QString ImageGen::formatToolResult(const QString &jobId,
                                   const QString &preset,
                                   int width,
                                   int height,
                                   qint64 seed)
{
    // Format the exact tool result text handed back to the chat model.
    return QStringLiteral("Image generated and shown to the user. job_id: %1; preset: %2; size: %3x%4; seed: %5. You have not seen the image.")
        .arg(jobId)
        .arg(preset)
        .arg(width)
        .arg(height)
        .arg(seed);
}

QString ImageGen::mapError(const QJsonObject &errorBody)
{
    // Map a server error JSON body to a plain, user-friendly error string.
    const QString code = errorBody.value(QStringLiteral("code")).toString();
    if (code == QStringLiteral("busy"))
    {
        return QStringLiteral("The image server is busy (one image rendering, up to 6 waiting). Try again in a few minutes.");
    }
    if (code == QStringLiteral("backend_error"))
    {
        const QString msg = errorBody.value(QStringLiteral("message")).toString();
        return msg.isEmpty()
                    ? QStringLiteral("The image server encountered an error. Try again later.")
                    : msg;
    }
    if (code == QStringLiteral("low_memory"))
    {
        return QStringLiteral("The image server is low on memory. Try again later.");
    }
    if (code == QStringLiteral("low_memory_abort"))
    {
        return QStringLiteral("The image server ran out of memory during generation. Try again later.");
    }
    if (code == QStringLiteral("timeout"))
    {
        return QStringLiteral("The image server timed out. Try again with a simpler prompt or lower quality.");
    }
    if (code == QStringLiteral("unauthorized"))
    {
        return QStringLiteral("The image server rejected the token. Check your settings.");
    }
    if (code == QStringLiteral("cancelled"))
    {
        return QStringLiteral("The image generation was cancelled.");
    }
    if (code == QStringLiteral("interrupted"))
    {
        return QStringLiteral("The image server restarted during generation. Try again.");
    }

    // Fallback: use the server's own message if available.
    const QString msg = errorBody.value(QStringLiteral("message")).toString();
    if (!msg.isEmpty())
    {
        return msg;
    }
    return QStringLiteral("Image generation failed.");
}

bool ImageGen::imageForToolResult(const QString &toolContent,
                                  const QString &dataDirectory,
                                  QString &jobId,
                                  QString &path,
                                  QJsonObject &fields)
{
    // Extract the job ID from tool-content text and look up the PNG + JSON files.
    // Returns true if both files exist, filling jobId, path and fields on success.
    const QRegularExpressionMatch match = kJobIdRegex.match(toolContent);
    if (!match.hasMatch())
    {
        return false;
    }
    jobId = match.captured(1);
    const QString dir = dataDirectory + QStringLiteral("/images");
    path = dir + QStringLiteral("/") + jobId + QStringLiteral(".png");
    const QString jsonPath = path.left(path.size() - 4) + QStringLiteral(".json");
    if (!QFile::exists(path) || !readJsonFile(jsonPath, fields))
    {
        return false;
    }
    // replaced_by is not a substitute. The original stays, and upscalesFor
    // lists the children under it.
    return true;
}

QString ImageGen::upscaleLabel(int scale, int width, int height)
{
    // The line above an upscale, with the factor and the saved pixel size.
    if (scale == 2 || scale == 4)
    {
        return QStringLiteral("Upscaled %1× — %2×%3").arg(scale).arg(width).arg(height);
    }
    return QStringLiteral("Upscaled — %1×%2").arg(width).arg(height);
}

bool ImageGen::readUpscale(const QString &dir, const QString &jobId,
                           const QJsonObject &fields, UpscaleImage &image)
{
    // A sidecar without its PNG is not an image the bubble can show.
    const QString path = dir + QLatin1Char('/') + jobId + QStringLiteral(".png");
    if (!QFile::exists(path))
    {
        return false;
    }
    image.jobId = jobId;
    image.path = path;
    image.prompt = fields.value(QStringLiteral("prompt")).toString();
    image.scale = fields.value(QStringLiteral("scale")).toInt();
    image.width = fields.value(QStringLiteral("width")).toInt();
    image.height = fields.value(QStringLiteral("height")).toInt();
    return true;
}

QList<ImageGen::UpscaleImage> ImageGen::upscalesFor(const QString &dataDirectory,
                                                    const QString &parentId,
                                                    const QJsonObject &parentFields)
{
    // Scan sidecars for parent, then an older replaced_by child that never
    // wrote parent. The original file is left as it is.
    QList<UpscaleImage> found;
    if (parentId.isEmpty())
    {
        return found;
    }
    const QString dirPath = dataDirectory + QStringLiteral("/images");
    const QDir dir(dirPath);
    const QStringList names =
        dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : names)
    {
        QJsonObject fields;
        if (!readJsonFile(dir.filePath(name), fields))
        {
            continue;
        }
        if (fields.value(QStringLiteral("parent")).toString() != parentId)
        {
            continue;
        }
        // The file stem is the job id the PNG was saved under.
        const QString jobId = name.left(name.size() - 5);
        UpscaleImage image;
        if (!readUpscale(dirPath, jobId, fields, image))
        {
            continue;
        }
        found.append(image);
    }
    const QString replaced = parentFields.value(QStringLiteral("replaced_by")).toString();
    if (!replaced.isEmpty() && replaced != parentId)
    {
        // Skip a child the parent scan already recorded.
        bool already = false;
        for (const UpscaleImage &image : found)
        {
            if (image.jobId == replaced)
            {
                already = true;
                break;
            }
        }
        if (!already)
        {
            QJsonObject fields;
            const QString jsonPath =
                dirPath + QLatin1Char('/') + replaced + QStringLiteral(".json");
            if (readJsonFile(jsonPath, fields))
            {
                UpscaleImage image;
                if (readUpscale(dirPath, replaced, fields, image))
                {
                    found.append(image);
                }
            }
        }
    }
    // Scale 2 before scale 4, then job id, so reopening shows the same order.
    std::sort(found.begin(), found.end(), [](const UpscaleImage &a, const UpscaleImage &b)
    {
        if (a.scale != b.scale)
        {
            return a.scale < b.scale;
        }
        return a.jobId < b.jobId;
    });
    return found;
}

bool ImageGen::offersScale(const QJsonObject &fields,
                           const QList<UpscaleImage> &upscales,
                           int scale)
{
    // An upscale result is not given another pair of buttons.
    if (!fields.value(QStringLiteral("parent")).toString().isEmpty())
    {
        return false;
    }
    for (const UpscaleImage &image : upscales)
    {
        // This factor is already saved, so its button stays hidden.
        if (image.scale == scale)
        {
            return false;
        }
    }
    if (offersUpscale(fields))
    {
        return true;
    }
    // An older build marked the parent upscaled and set replaced_by. The
    // factor that was not produced can still be requested.
    const bool marked = fields.value(QStringLiteral("upscaled")).toBool();
    const bool linked = !fields.value(QStringLiteral("replaced_by")).toString().isEmpty()
        || !upscales.isEmpty();
    return marked && linked;
}

bool ImageGen::presetAlreadyUpscaled(const QString &preset)
{
    // Older sidecars omit upscaled. Those scene and people images were
    // upscaled by the server, so the buttons stay hidden. New files write
    // the field themselves and are not upscaled.
    return preset == QLatin1String("zimage-scene")
        || preset == QLatin1String("zimage-people");
}

bool ImageGen::offersUpscale(const QJsonObject &fields)
{
    // Trust the flag when the sidecar has one. Older files omit it.
    if (fields.contains(QStringLiteral("upscaled")))
    {
        return !fields.value(QStringLiteral("upscaled")).toBool();
    }
    // An upscale result names its parent, so it is already the larger image.
    if (!fields.value(QStringLiteral("parent")).toString().isEmpty())
    {
        return false;
    }
    return !presetAlreadyUpscaled(fields.value(QStringLiteral("preset")).toString());
}

bool ImageGen::readJsonFile(const QString &path, QJsonObject &fields)
{
    // Read one sidecar. False when the file is missing or not JSON.
    QFile jsonFile(path);
    if (!jsonFile.open(QIODevice::ReadOnly))
    {
        return false;
    }
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError)
    {
        return false;
    }
    fields = doc.object();
    return true;
}

QJsonObject ImageGen::upscaleBody(int scale)
{
    // The server treats a missing scale as its old default. Always send 2 or 4.
    QJsonObject body;
    body.insert(QStringLiteral("async"), true);
    body.insert(QStringLiteral("scale"), scale);
    return body;
}

void ImageGen::start(const ImageJobSpec &spec)
{
    // Start the full job lifecycle: submit -> poll -> download -> save PNG + JSON.
    // Only one job at a time; the previous one is abandoned if any.
    if (m_currentJob)
    {
        cancel();
    }
    auto *job = new Job;
    job->baseUrl = spec.baseUrl;
    job->token = spec.token;
    job->dataDirectory = spec.dataDirectory;
    job->seed = spec.seed;
    job->prompt = spec.prompt;
    job->preset = spec.preset;
    job->aspect = spec.aspect;
    m_currentJob = job;
    m_jobs.append(job);
    // Connect the poll timer; use `this` as context since Job is not a QObject.
    connect(&job->pollTimer, &QTimer::timeout, this, [this, job]()
            { startPolling(job); });
    // Submit the job immediately; the callback will set up the poll timer.
    submit(job);
}

void ImageGen::upscale(const UpscaleJobSpec &spec)
{
    // Same single-job lifecycle as start(), aimed at a finished image.
    if (spec.scale != 2 && spec.scale != 4)
    {
        emit failed({}, QStringLiteral("scale must be 2 or 4"));
        return;
    }
    const QString dir = spec.dataDirectory + QStringLiteral("/images");
    const QString parentPng =
        dir + QStringLiteral("/") + spec.parentId + QStringLiteral(".png");
    const QString parentJson =
        parentPng.left(parentPng.size() - 4) + QStringLiteral(".json");
    QJsonObject parentFields;
    if (!QFile::exists(parentPng) || !readJsonFile(parentJson, parentFields))
    {
        emit failed(spec.parentId, QStringLiteral("no image to upscale"));
        return;
    }
    // One request at a time. Dropping the previous one matches start().
    if (m_currentJob)
    {
        cancel();
    }
    auto *job = new Job;
    job->baseUrl = spec.baseUrl;
    job->token = spec.token;
    job->dataDirectory = spec.dataDirectory;
    job->prompt = parentFields.value(QStringLiteral("prompt")).toString();
    job->preset = parentFields.value(QStringLiteral("preset")).toString();
    job->seed = parentFields.value(QStringLiteral("seed")).toInteger(-1);
    job->upscale = true;
    job->scale = spec.scale;
    job->parentId = spec.parentId;
    m_currentJob = job;
    m_jobs.append(job);
    // Connect the poll timer; use `this` as context since Job is not a QObject.
    connect(&job->pollTimer, &QTimer::timeout, this, [this, job]()
            { startPolling(job); });
    const QString path =
        QStringLiteral("/v1/jobs/") + spec.parentId + QStringLiteral("/upscale");
    postJob(job, path, upscaleBody(spec.scale));
}

void ImageGen::cancel()
{
    // Cancel the currently running job by sending DELETE /v1/jobs/{id}.
    if (!m_currentJob)
    {
        return;
    }
    m_currentJob->cancelled = true;
    m_currentJob->pollTimer.stop();
    // Stop any in-flight request (Fix 1: disconnect before abort to prevent re-entry).
    if (m_currentJob->reply)
    {
        m_currentJob->reply->disconnect(this);
        m_currentJob->reply->abort();
        m_currentJob->reply->deleteLater();
        m_currentJob->reply = nullptr;
    }
    // Fix 5: if we don't have an id yet (cancel during submit), clean up now.
    if (m_currentJob->id.isEmpty())
    {
        cleanupJob(m_currentJob);
        emit failed({}, QStringLiteral("cancelled"));
        return;
    }
    // Send DELETE to cancel the job on the server.
    sendDelete(m_currentJob);
    // Clean up locally and emit failed with "cancelled".
    const QString jobId = m_currentJob->id;
    cleanupJob(m_currentJob);
    emit failed(jobId, QStringLiteral("cancelled"));
}

void ImageGen::testConnection(const QUrl &baseUrl)
{
    // Test the connection to the image server (GET /v1/health).
    // Uses connectionTested signal so it doesn't conflict with job finished/failed.
    QNetworkRequest req(QUrl(trimTrailingSlash(baseUrl.toString()) + "/v1/health"));
    req.setTransferTimeout(10000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, baseUrl]()
            {
                reply->deleteLater();
                const QByteArray raw = reply->readAll();
                if (reply->error() != QNetworkReply::NoError)
                {
                    emit connectionTested(false, reply->errorString());
                    return;
                }
                QJsonParseError err;
                const QJsonObject obj =
                    QJsonDocument::fromJson(raw, &err).object();
                if (err.error != QJsonParseError::NoError)
                {
                    emit connectionTested(false, QStringLiteral("invalid response from image server"));
                    return;
                }
                if (obj.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
                {
                    emit connectionTested(false, QStringLiteral("image server reports unhealthy"));
                    return;
                }
                emit connectionTested(true, QStringLiteral("ok"));
            });
}

QJsonObject ImageGen::submitBody(const QString &preset, const QString &prompt,
                                 const QString &aspect, qint64 seed)
{
    // Build the generation request; async so the job is polled.
    // No preset asks the server to upscale. The buttons do that later.
    QJsonObject body;
    body.insert(QStringLiteral("model"), preset);
    body.insert(QStringLiteral("prompt"), prompt);
    body.insert(QStringLiteral("async"), true);
    body.insert(QStringLiteral("size"), aspect);
    if (seed >= 0)
    {
        body.insert(QStringLiteral("seed"), seed);
    }
    return body;
}

void ImageGen::submit(Job *job)
{
    // POST /v1/images/generations with async=true.
    const QJsonObject body = submitBody(job->preset, job->prompt, job->aspect, job->seed);
    postJob(job, QStringLiteral("/v1/images/generations"), body);
}

void ImageGen::postJob(Job *job, const QString &path, const QJsonObject &body)
{
    // POST a generation or an upscale. On success (202), sets up the poll timer.
    // On error, emits failed immediately.
    QNetworkRequest req(QUrl(trimTrailingSlash(job->baseUrl.toString()) + path));
    req.setTransferTimeout(30000);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + job->token).toUtf8());

    QNetworkReply *reply =
        m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    job->reply = reply; // track for cancel/cleanup (Fix 1: QPointer will null after deleteLater)
    connect(reply, &QNetworkReply::finished, this, [this, reply, job]()
            {
                // Clear the tracked reply pointer before processing (Fix 1).
                job->reply = nullptr;
                reply->deleteLater();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray raw = reply->readAll();

                // Check HTTP status code first. Even if reply->error() is set,
                // a valid HTTP status means the server responded and we should
                // parse the response body.
                if (status >= 200 && status < 300)
                {
                    // Job created successfully.
                    QJsonParseError parseErr;
                    const QJsonObject obj =
                        QJsonDocument::fromJson(raw, &parseErr).object();
                    if (parseErr.error != QJsonParseError::NoError)
                    {
                        emit failed({}, QStringLiteral("invalid submit response"));
                        cleanupJob(job); // Fix 5: cleanup on submit parse error
                        return;
                    }
                    job->id = obj.value(QStringLiteral("id")).toString();
                    if (job->id.isEmpty())
                    {
                        emit failed({},
                                    QStringLiteral("the image server did not return a job id"));
                        cleanupJob(job);
                        return;
                    }
                    // Keep the parent preset when an upscale reply omits model.
                    const QString model = obj.value(QStringLiteral("model")).toString();
                    if (!model.isEmpty())
                    {
                        job->preset = model;
                    }
                    const QString aspect = obj.value(QStringLiteral("aspect")).toString();
                    if (!aspect.isEmpty())
                    {
                        job->aspect = aspect;
                    }
                    // Start polling after a short delay.
                    job->pollTimer.start(2000);
                }
                else
                {
                    // Error response (400, 401, 429, etc.) or true network error.
                    QJsonParseError parseErr;
                    const QJsonObject respBody =
                        QJsonDocument::fromJson(raw, &parseErr).object();
                    const QJsonObject error =
                        respBody.value(QStringLiteral("error")).toObject();
                    if (!error.isEmpty())
                    {
                        emit failed({}, mapError(error));
                    }
                    else if (status >= 100)
                    {
                        emit failed({}, QStringLiteral("HTTP %1").arg(status));
                    }
                    else
                    {
                        // True network error (no HTTP status).
                        emit failed({}, reply->errorString());
                    }
                    cleanupJob(job); // Fix 5: cleanup on submit HTTP/network error
                }
            });
}

void ImageGen::startPolling(Job *job)
{
    // Begin polling the job status.
    // First poll happens immediately, then every 2 seconds.
    doPoll(job);
}

void ImageGen::doPoll(Job *job)
{
    // Stop the timer so it cannot fire while a request is in flight.
    // If the timer fires during the request, the handler becomes a no-op
    // because handlePollResponse always resets the timer after completing.
    job->pollTimer.stop();
    // Send GET /v1/jobs/{id} to check the job status.
    QNetworkRequest req(QUrl(trimTrailingSlash(job->baseUrl.toString()) +
                             "/v1/jobs/" + job->id));
    req.setTransferTimeout(30000);
    req.setRawHeader("Authorization", ("Bearer " + job->token).toUtf8());

    QNetworkReply *reply = m_nam.get(req);
    job->reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, job]()
            {
                job->reply = nullptr; // Fix 1: clear tracked reply.
                handlePollResponse(job, reply);
            });
}

void ImageGen::handlePollResponse(Job *job, QNetworkReply *reply)
{
    // Handle a poll response: check status and act accordingly.
    if (job->cancelled)
    {
        reply->deleteLater();
        return;
    }
    reply->deleteLater();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = reply->readAll();

    if (status != 200)
    {
        // Network or HTTP error.
        handlePollError(job, reply->errorString());
        return;
    }

    QJsonParseError parseErr;
    const QJsonObject obj = QJsonDocument::fromJson(raw, &parseErr).object();
    if (parseErr.error != QJsonParseError::NoError)
    {
        handlePollError(job, QStringLiteral("invalid poll response"));
        return;
    }

    const QString s = obj.value(QStringLiteral("status")).toString();

    if (s == QStringLiteral("succeeded"))
    {
        // Job done - store result dimensions/seed and download the image.
        job->consecutiveFailures = 0;
        const QJsonObject result = obj.value(QStringLiteral("result")).toObject();
        job->width = result.value(QStringLiteral("width")).toInt();
        job->height = result.value(QStringLiteral("height")).toInt();
        job->seed = result.value(QStringLiteral("seed")).toVariant().toLongLong();
        downloadImage(job);
    }
    else if (s == QStringLiteral("failed") || s == QStringLiteral("cancelled"))
    {
        // Job ended on the server side.
        const QJsonObject err =
            obj.value(QStringLiteral("error")).toObject();
        QString msg;
        if (!err.isEmpty())
        {
            msg = mapError(err);
        }
        else
        {
            msg = QStringLiteral("Image generation %1").arg(s);
        }
        emit failed(job->id, msg);
        cleanupJob(job);
    }
    else
    {
        // Still in progress (queued or running).
        // Fail on any unexpected status to avoid infinite polling.
        if (s != QStringLiteral("queued") && s != QStringLiteral("running"))
        {
            emit failed(job->id,
                        QStringLiteral("unexpected job status: %1").arg(s));
            cleanupJob(job);
            return;
        }
        job->consecutiveFailures = 0;
        const double percent =
            obj.value(QStringLiteral("progress")).toObject()
                .value(QStringLiteral("percent"))
                .toDouble();
        const QString stage =
            obj.value(QStringLiteral("progress")).toObject()
                .value(QStringLiteral("stage"))
                .toString();
        const int queuePos =
            obj.value(QStringLiteral("queue_position")).toInt();
        // zimage-gw sends estimate_s with one decimal (e.g. 205.3); toInt() would
        // give 0 for any value that is not a whole number, so round the double.
        const int estimate =
            qRound(obj.value(QStringLiteral("estimate_s")).toDouble());
        const QString text =
            formatProgressText(s, percent, stage, queuePos, estimate);
        emit progress(job->id, text);
        // Restart the poll timer.
        job->pollTimer.start(2000);
    }
}

void ImageGen::handlePollError(Job *job, const QString &error)
{
    // Handle a poll error: retry up to 5 times, then give up.
    job->consecutiveFailures++;
    if (job->consecutiveFailures >= 5)
    {
        emit failed(job->id,
                    QStringLiteral("lost contact with the image server"));
        cleanupJob(job);
        return;
    }
    // Restart the poll timer.
    job->pollTimer.start(2000);
}

void ImageGen::downloadImage(Job *job)
{
    // GET /v1/jobs/{id}/image to download the PNG.
    QUrl url(
        trimTrailingSlash(job->baseUrl.toString()) + "/v1/jobs/" + job->id +
        "/image");
    QNetworkRequest req(url);
    req.setTransferTimeout(120000); // 120 s for image download
    req.setRawHeader("Authorization", ("Bearer " + job->token).toUtf8());

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, job]()
            {
                if (job->cancelled)
                {
                    reply->deleteLater();
                    return;
                }
                reply->deleteLater();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                        .toInt();
                const QByteArray data = reply->readAll();

                if (reply->error() != QNetworkReply::NoError || status != 200 ||
                    data.isEmpty())
                {
                    QString err = reply->errorString();
                    if (status == 404)
                    {
                        err =
                            QStringLiteral("image not found for job %1").arg(job->id);
                    }
                    emit failed(job->id, err);
                    cleanupJob(job);
                    return;
                }
                // Width and height come from the poll response result (Fix 3).
                saveFiles(job, data, job->width, job->height);
            });
}

void ImageGen::saveFiles(Job *job, const QByteArray &imageData, int width,
                         int height)
{
    // Save the PNG and its JSON metadata atomically under the data directory.
    const QString dir = job->dataDirectory + QStringLiteral("/images");
    QDir dirObj(dir);
    if (!dirObj.exists() && !dirObj.mkpath(QStringLiteral(".")))
    {
        emit failed(
            job->id,
            QStringLiteral("cannot create image directory %1").arg(dir));
        cleanupJob(job);
        return;
    }

    const QString pngPath =
        dir + QStringLiteral("/") + job->id + QStringLiteral(".png");
    const QString jsonPath =
        pngPath.left(pngPath.size() - 4) + QStringLiteral(".json");

    // Build the metadata JSON.
    QJsonObject fields;
    fields.insert(QStringLiteral("job_id"), job->id);
    fields.insert(QStringLiteral("prompt"), job->prompt);
    fields.insert(QStringLiteral("preset"), job->preset);
    fields.insert(QStringLiteral("seed"), job->seed);
    fields.insert(QStringLiteral("width"), width);
    fields.insert(QStringLiteral("height"), height);
    fields.insert(QStringLiteral("created"),
                  QDateTime::currentDateTime().toString(Qt::ISODate));
    // A generation is not upscaled. Only an upscale job sets the flag.
    // Older files that omit it are handled in offersUpscale.
    const bool upscaled = job->upscale;
    fields.insert(QStringLiteral("upscaled"), upscaled);
    if (job->upscale)
    {
        // The new file names the image it was made from and the factor asked for.
        fields.insert(QStringLiteral("parent"), job->parentId);
        fields.insert(QStringLiteral("scale"), job->scale);
    }

    // Write both files atomically. The original PNG and JSON stay unchanged.
    if (!writeAtomic(jsonPath,
                     QJsonDocument(fields).toJson(QJsonDocument::Compact)) ||
        !writeAtomic(pngPath, imageData))
    {
        emit failed(job->id,
                    QStringLiteral("failed to save image files"));
        cleanupJob(job);
        return;
    }

    // Emit finished with the PNG path.
    emit finished(job->id, pngPath);
    cleanupJob(job);
}

bool ImageGen::writeAtomic(const QString &path, const QByteArray &data)
{
    // Write data to a file atomically using QSaveFile.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    file.write(data);
    return file.commit();
}

void ImageGen::sendDelete(Job *job)
{
    // Send DELETE /v1/jobs/{id} to cancel the job on the server.
    if (job->id.isEmpty())
    {
        return;
    }
    QNetworkRequest req(QUrl(trimTrailingSlash(job->baseUrl.toString()) +
                             "/v1/jobs/" + job->id));
    req.setTransferTimeout(30000);
    req.setRawHeader("Authorization", ("Bearer " + job->token).toUtf8());
    QNetworkReply *reply = m_nam.deleteResource(req);
    connect(reply, &QNetworkReply::finished, reply, [reply]()
            { reply->deleteLater(); });
}

void ImageGen::cleanupJob(Job *job)
{
    // Remove a completed job from the tracking list.
    // Set cancelled first so finished handlers become no-ops (Fix 1).
    job->cancelled = true;
    job->pollTimer.stop();
    if (job->reply)
    {
        // Disconnect before abort to prevent re-entering a finished handler
        // that might emit a second signal or delete the job twice (Fix 1).
        job->reply->disconnect(this);
        job->reply->abort();
        job->reply->deleteLater();
        job->reply = nullptr;
    }
    m_jobs.removeAll(job);
    if (m_currentJob == job)
    {
        m_currentJob = nullptr;
    }
    delete job;
}
