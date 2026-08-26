#include "controllers/ChatController.h"
#include "Util.h"
#include "artifacts/ArtifactExtractor.h"
#include "artifacts/Attach.h"
#include "artifacts/DocxExport.h"
#include "artifacts/HtmlDocument.h"
#include "artifacts/SpreadsheetExtract.h"
#include "openai/ToolCallXml.h"

#include <QBuffer>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QThread>
#include <QImage>
#include <QTextDocument>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QVariantMap>
#include <QMimeData>
#include <QMimeDatabase>
#include <QDateTime>
#include <QLocale>
#include <QStringList>
#include <QTimeZone>
#include <QUrl>
#include <QVariant>
#include <QWindow>

static const char *kSystem = R"(You are Shammy, a local desktop assistant talking to a model served over an OpenAI-compatible API (Ollama, llama.cpp, or similar).

If tools are provided, call them instead of describing what they would do. Do not invent tool results.
When web_fetch is listed, use it to read a URL the user shares (README, docs, article). Do not say you cannot browse.
When web_search is listed, use it for open-ended live questions without a specific URL.)";

static QString localTimePromptLine()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QTimeZone tz = QTimeZone::systemTimeZone();
    const QString iana = QString::fromUtf8(tz.id());
    const int secs = tz.offsetFromUtc(now);
    const QChar sign = secs >= 0 ? QLatin1Char('+') : QLatin1Char('-');
    const int abs = qAbs(secs);
    const QString off = QStringLiteral("%1%2:%3")
                            .arg(sign)
                            .arg(abs / 3600, 2, 10, QLatin1Char('0'))
                            .arg((abs % 3600) / 60, 2, 10, QLatin1Char('0'));
    const QLocale en(QLocale::English, QLocale::UnitedStates);
    const QString stamp = en.toString(now, QStringLiteral("dddd, dd MMMM yyyy, HH:mm"));
    QString zone = iana;
    const QString abbr = tz.abbreviation(now);
    if (!abbr.isEmpty() && abbr != iana)
        zone += QStringLiteral(", %1").arg(abbr);
    zone += QStringLiteral(", UTC%1").arg(off);
    return QStringLiteral(
               "The user's local date and time is %1 (%2). Treat this as today unless they say otherwise.")
        .arg(stamp, zone);
}

static const char *kArtifactInstructions = R"(
When you produce a substantial standalone document, UI, diagram, or code file the user may want to keep or iterate on, wrap it in:
<artifact identifier="short-slug" type="MIME" title="Human title" language="optional">
...content...
</artifact>
Common types: text/html, image/svg+xml, text/markdown, text/plain, text/x-python, text/javascript, text/css.

For interactive pages (dashboards, visualizations, tool results the user can click or filter), emit a complete HTML document as type text/html: <!DOCTYPE html>, utf-8, CSS and JS inside the artifact. CDN <script src> and <link> are fine. Do not dump the page into the chat bubble; the desktop client renders the artifact in a side pane with JavaScript enabled.)";

static const char *kFinalWriteNudge = R"(
The tool results are already in the transcript. Tool calling is disabled for this turn.
Never emit <tool_call>, <function=, or similar XML — those are not executed.
Write the complete final answer now. If the user asked for HTML, markdown, a page, or a report,
emit a full <artifact type="text/html" ...> (or text/markdown) document. Do not describe what you will do next.)";

static bool looksLikeToolPreface(const QString &content)
{
    if (ToolCallXml::looksLike(content))
        return true;
    const QString t = content.trimmed();
    if (t.size() > 800)
        return false;
    if (t.contains(QLatin1String("<artifact"), Qt::CaseInsensitive)
        || t.contains(QLatin1String("<!doctype"), Qt::CaseInsensitive)
        || t.contains(QLatin1String("<html"), Qt::CaseInsensitive))
        return false;
    if (!ArtifactExtractor::extract(t).isEmpty())
        return false;
    const QString l = t.toLower();
    return l.contains(QLatin1String("let me"))
        || l.contains(QLatin1String("i'll "))
        || l.contains(QLatin1String("i will "))
        || l.contains(QLatin1String("now i have"))
        || l.endsWith(QLatin1Char(':'));
}

ChatController::ChatController(Store *store, OpenAiClient *client, McpController *mcp,
                               ProjectController *projects, SettingsController *settings,
                               QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_client(client)
    , m_mcp(mcp)
    , m_projects(projects)
    , m_settings(settings)
{
    connect(m_client, &OpenAiClient::chunk, this, &ChatController::onChunk);
    connect(m_client, &OpenAiClient::reasoning, this, &ChatController::onReasoning);
    connect(m_client, &OpenAiClient::toolCallDelta, this, &ChatController::onToolDelta);
    connect(m_client, &OpenAiClient::finished, this, &ChatController::onFinished);
    connect(m_client, &OpenAiClient::failed, this, &ChatController::onFailed);
    connect(m_client, &OpenAiClient::usage, this, &ChatController::onUsage);
    connect(m_client, &OpenAiClient::completed, this, &ChatController::onCompactCompleted);
    connect(m_client, &OpenAiClient::completeFailed, this, &ChatController::onCompactFailed);
    connect(m_settings, &SettingsController::contextSizeChanged, this, &ChatController::contextUsageChanged);
    connect(m_store, &Store::conversationsChanged, this, &ChatController::reloadHistory);
    connect(m_projects, &ProjectController::currentProjectIdChanged, this, [this]()
    {
        reloadHistory();
    });
    connect(m_settings, &SettingsController::enableArtifactsChanged, this, [this]()
    {
        if (!m_settings->enableArtifacts())
            setArtifactPaneOpen(false);
    });
    connect(m_settings, &SettingsController::modelCapsChanged, this, [this]()
    {
        if (!m_settings->modelThinking() && !m_thinkingMode.isEmpty())
        {
            setThinkingMode({});
        }
        emit webSearchAvailableChanged();
    });
    connect(m_settings, &SettingsController::webSearchChanged, this, &ChatController::webSearchAvailableChanged);
    connect(this, &ChatController::currentArtifactVersionChanged, this,
            &ChatController::wordExportAvailableChanged);
    connect(m_settings, &SettingsController::officeBinaryPathChanged, this,
            &ChatController::wordExportAvailableChanged);
    applyDefaultThinking();
    reloadHistory();
    refreshContextUsage();
    if (qApp)
    {
        qApp->installEventFilter(this);
    }
}

void ChatController::setComposerText(const QString &t)
{
    if (m_composer == t)
        return;
    m_composer = t;
    emit composerTextChanged();
}

void ChatController::replyToSelection(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || m_replyQuote == t)
        return;
    m_replyQuote = t;
    emit replyQuoteChanged();
}

void ChatController::clearReplyQuote()
{
    if (m_replyQuote.isEmpty())
        return;
    m_replyQuote.clear();
    emit replyQuoteChanged();
}

void ChatController::copyText(const QString &text)
{
    if (text.isEmpty())
        return;
    QClipboard *cb = QGuiApplication::clipboard();
    if (cb)
        cb->setText(text);
}

void ChatController::setArtifactPaneOpen(bool v)
{
    if (m_artifactPaneOpen == v)
        return;
    m_artifactPaneOpen = v;
    if (v)
        writePreviewFile();
    emit artifactPaneOpenChanged();
}

void ChatController::setSearchQuery(const QString &q)
{
    if (m_search == q)
        return;
    m_search = q;
    emit searchQueryChanged();
    reloadHistory();
}

QString ChatController::emptyHint() const
{
    if (m_private)
    {
        return QStringLiteral("Private chat. This session is not saved to history.");
    }
    if (m_settings->models()->rowCount() == 0)
    {
        return QStringLiteral(
            "No models on this backend. Pull one with ollama pull, or check the URL in Settings.");
    }
    return QStringLiteral("Send a message to start chatting.");
}

void ChatController::reloadHistory()
{
    QList<Conversation> all;
    if (!m_search.trimmed().isEmpty())
    {
        all = m_store->search(m_search);
    }
    else
    {
        all = m_store->conversations(m_projects->currentProjectId());
    }
    QList<Conversation> fav;
    QList<Conversation> rest;
    for (const Conversation &c : all)
    {
        if (c.pinned)
        {
            fav.append(c);
        }
        else
        {
            rest.append(c);
        }
    }
    m_favorites.setItems(fav);
    m_conversations.setItems(rest);
}

void ChatController::setError(const QString &s)
{
    m_errorBanner = s;
    emit errorBannerChanged();
}

void ChatController::applyDefaultThinking()
{
    const QString next = m_settings->modelThinking() ? m_settings->defaultThinkingMode() : QString();
    if (m_thinkingMode == next)
        return;
    m_thinkingMode = next;
    emit thinkingModeChanged();
}

void ChatController::persistMessage(const Message &m)
{
    if (m_private)
    {
        return;
    }
    m_store->upsertMessage(m);
}

void ChatController::persistThinkingMode()
{
    if (m_private || m_convId.isEmpty())
        return;
    Conversation c = m_store->conversation(m_convId);
    if (c.id.isEmpty())
        return;
    c.reasoningEffort = m_thinkingMode;
    c.updatedAt = nowMs();
    m_store->upsertConversation(c);
}

void ChatController::setThinkingMode(const QString &mode)
{
    QString normalized = mode.trimmed().toLower();
    if (!m_settings->modelThinking())
    {
        normalized.clear();
    }
    if (normalized == QLatin1String("off") || normalized == QLatin1String("none"))
        normalized.clear();
    if (m_thinkingMode == normalized)
        return;
    m_thinkingMode = normalized;
    persistThinkingMode();
    emit thinkingModeChanged();
}

QString ChatController::thinkingModeLabel() const
{
    if (m_thinkingMode.isEmpty())
        return QStringLiteral("Off");
    if (m_thinkingMode == QLatin1String("low"))
        return QStringLiteral("Low");
    if (m_thinkingMode == QLatin1String("high"))
        return QStringLiteral("High");
    return QStringLiteral("Medium");
}

int ChatController::estimateTokens(const QString &text)
{
    if (text.isEmpty())
    {
        return 0;
    }
    return qMax(1, (text.size() + 3) / 4);
}

int ChatController::contextLimit() const
{
    return m_settings->contextSize();
}

static QString formatUsageSize(int n)
{
    if (n < 1024)
    {
        return QString::number(n) + QStringLiteral(" B");
    }
    if (n % 1024 == 0)
    {
        return QString::number(n / 1024) + QStringLiteral(" KB");
    }
    return QString::number(double(n) / 1024.0, 'f', 1) + QStringLiteral(" KB");
}

QString ChatController::contextUsageLabel() const
{
    const int used = m_contextUsed;
    const int limit = contextLimit();
    QString label = formatUsageSize(used) + QStringLiteral(" / ") + formatUsageSize(limit);
    if (limit > 0)
    {
        const int pct = qRound(100.0 * double(used) / double(limit));
        label += QLatin1String(" (") + QString::number(pct) + QLatin1Char('%') + QLatin1Char(')');
    }
    return label;
}

void ChatController::refreshContextUsage()
{
    int used = 0;
    for (const ChatMessage &m : m_messages.all())
    {
        used += estimateTokens(m.content);
        used += estimateTokens(m.reasoning);
    }
    if (used != m_contextUsed)
    {
        m_contextUsed = used;
        emit contextUsageChanged();
    }
}

void ChatController::onUsage(int promptTokens, int completionTokens, int totalTokens)
{
    const int used = totalTokens > 0 ? totalTokens : (promptTokens + completionTokens);
    if (used <= 0)
    {
        return;
    }
    m_promptTokensEst = promptTokens;
    if (used != m_contextUsed)
    {
        m_contextUsed = used;
        emit contextUsageChanged();
    }
}

void ChatController::ensureConversation()
{
    if (!m_convId.isEmpty())
        return;
    m_convId = newId();
    if (m_private)
    {
        emit conversationChanged();
        return;
    }
    Conversation c;
    c.id = m_convId;
    c.projectId = m_projects->currentProjectId();
    c.title = QStringLiteral("New chat");
    c.backendId = m_settings->currentBackendId();
    c.model = m_settings->currentModel();
    c.reasoningEffort = m_thinkingMode;
    c.createdAt = c.updatedAt = nowMs();
    m_store->upsertConversation(c);
    emit conversationChanged();
}

void ChatController::newChat()
{
    beginSession(false);
}

void ChatController::newPrivateChat()
{
    beginSession(true);
}

void ChatController::startProjectChat(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() && m_pending.isEmpty())
    {
        beginSession(false);
        m_projects->showChat();
        return;
    }
    beginSession(false);
    m_composer = t;
    emit composerTextChanged();
    m_projects->showChat();
    send();
}

void ChatController::beginSession(bool priv)
{
    if (m_streaming || m_compacting)
        stop();
    m_private = priv;
    m_convId.clear();
    m_messages.clear();
    m_artifacts.setItems({});
    m_accTools.clear();
    m_pendingToolQueue = {};
    m_toolRounds = 0;
    m_forceFinalWrite = false;
    m_finalWriteAttempts = 0;
    m_sessionAllow.clear();
    m_permOpen = false;
    applyDefaultThinking();
    emit conversationChanged();
    emit permissionChanged();
    emit emptyHintChanged();
    loadArtifacts();
    m_suppressAutoCompact = false;
    setCompactStatus({});
    clearReplyQuote();
    refreshContextUsage();
}

void ChatController::openConversation(const QString &id)
{
    if (m_streaming || m_compacting)
        stop();
    m_private = false;
    m_convId = id;
    QVector<ChatMessage> msgs;
    for (const Message &m : m_store->messages(id))
    {
        ChatMessage cm;
        cm.id = m.id;
        cm.conversationId = m.conversationId;
        cm.role = m.role;
        cm.content = m.content;
        cm.reasoning = m.reasoning;
        cm.toolCallsJson = m.toolCallsJson;
        cm.toolCallId = m.toolCallId;
        cm.createdAt = m.createdAt;
        msgs.append(cm);
    }
    m_messages.setMessages(msgs);
    m_toolRounds = 0;
    m_forceFinalWrite = false;
    m_finalWriteAttempts = 0;
    const Conversation c = m_store->conversation(id);
    const QString model = m_settings->availableModel(c.model);
    if (!model.isEmpty())
        m_settings->setCurrentModel(model);
    if (!c.model.isEmpty() && model != c.model && !m_settings->models()->ids().isEmpty())
    {
        Conversation u = c;
        u.model = model;
        m_store->upsertConversation(u);
    }
    if (!c.backendId.isEmpty())
        m_settings->setCurrentBackendId(c.backendId);
    m_thinkingMode = c.reasoningEffort;
    emit thinkingModeChanged();
    emit conversationChanged();
    loadArtifacts();
    m_suppressAutoCompact = false;
    setCompactStatus({});
    clearReplyQuote();
    refreshContextUsage();
}

QStringList ChatController::pendingAttachments() const
{
    QStringList out;
    out.reserve(m_pending.size());
    for (const PendingAttach &a : m_pending)
    {
        out.append(a.label + QStringLiteral(" — ") + a.rest);
    }
    return out;
}

void ChatController::clearPending()
{
    if (m_pending.isEmpty())
    {
        return;
    }
    m_pending.clear();
    emit pendingAttachmentsChanged();
}

void ChatController::setFileDropHover(bool v)
{
    if (m_fileDropHover == v)
    {
        return;
    }
    m_fileDropHover = v;
    emit fileDropHoverChanged();
}

void ChatController::enqueuePaste(const QString &text)
{
    PendingAttach a;
    a.type = PendingAttach::Paste;
    a.label = Attach::pastedChipLabel(text.size());
    a.rest = QStringLiteral("pasted-text");
    a.paste = text;
    m_pending.append(a);
    emit pendingAttachmentsChanged();
    setError({});
}

bool ChatController::tryAttachPath(const QString &path, QString *error)
{
    const QFileInfo fi(path);
    const auto fail = [&](const QString &msg) -> bool
    {
        if (error)
        {
            *error = msg;
        }
        return false;
    };
    if (!fi.exists())
    {
        return fail(QStringLiteral("Can't attach `%1`: file not found.").arg(fi.fileName().isEmpty()
                                                                                ? path
                                                                                : fi.fileName()));
    }
    if (fi.isDir())
    {
        return fail(QStringLiteral("Can't attach `%1`: that's a folder.").arg(fi.fileName()));
    }
    if (!fi.isFile())
    {
        return fail(QStringLiteral("Can't attach `%1`: not a regular file.").arg(fi.fileName()));
    }

    const Attach::Kind kind = Attach::kindForPath(path);
    if (kind == Attach::Kind::Image)
    {
        if (!m_settings->modelVision())
        {
            return fail(QStringLiteral(
                            "Can't attach `%1`: this model is not configured for vision. "
                            "Enable Vision in Settings → Models.")
                            .arg(fi.fileName()));
        }
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
        {
            return fail(QStringLiteral("Can't attach `%1`: could not read the file.")
                            .arg(fi.fileName()));
        }
        const QString mime = QMimeDatabase().mimeTypeForFile(fi).name();
        PendingAttach a;
        a.type = PendingAttach::Image;
        a.label = fi.fileName();
        a.rest = path;
        a.path = path;
        a.image.type = QStringLiteral("image");
        a.image.imageDataUrl = QStringLiteral("data:%1;base64,%2")
                                   .arg(mime, QString::fromLatin1(f.readAll().toBase64()));
        a.image.title = fi.fileName();
        m_pending.append(a);
        emit pendingAttachmentsChanged();
        return true;
    }
    if (kind == Attach::Kind::Spreadsheet)
    {
        if (!DocxExport::available(m_settings->officeBinaryPath()))
        {
            return fail(
                QStringLiteral(
                    "Can't attach `%1`: LibreOffice/OpenOffice is needed to convert spreadsheets.")
                    .arg(fi.fileName()));
        }
    }
    else if (kind == Attach::Kind::Unsupported)
    {
        return fail(QStringLiteral("Can't attach `%1`: that file type isn't supported.")
                        .arg(fi.fileName()));
    }

    PendingAttach a;
    a.type = PendingAttach::File;
    a.label = fi.fileName();
    a.rest = path;
    a.path = path;
    m_pending.append(a);
    emit pendingAttachmentsChanged();
    return true;
}

void ChatController::attachFile(const QString &urlOrPath)
{
    QString path = urlOrPath;
    const QUrl u(urlOrPath);
    if (u.isLocalFile())
    {
        path = u.toLocalFile();
    }
    else if (urlOrPath.startsWith(QLatin1String("file:")))
    {
        path = QUrl(urlOrPath).toLocalFile();
    }
    QString err;
    if (tryAttachPath(path, &err))
    {
        setError({});
        return;
    }
    if (!err.isEmpty())
    {
        setError(err);
    }
}

bool ChatController::enqueueImageBytes(const QByteArray &bytes, const QString &mime, const QString &title)
{
    if (bytes.isEmpty())
    {
        return false;
    }
    if (!m_settings->modelVision())
    {
        setError(QStringLiteral(
            "This model is not configured for vision. Enable Vision in Settings → Models."));
        return false;
    }
    PendingAttach a;
    a.type = PendingAttach::Image;
    a.label = title;
    a.rest = QStringLiteral("clipboard");
    a.image.type = QStringLiteral("image");
    a.image.imageDataUrl = QStringLiteral("data:%1;base64,%2")
                               .arg(mime, QString::fromLatin1(bytes.toBase64()));
    a.image.title = title;
    m_pending.append(a);
    emit pendingAttachmentsChanged();
    setError({});
    return true;
}

bool ChatController::pasteClipboardImage()
{
    const QClipboard *clip = QGuiApplication::clipboard();
    if (!clip)
    {
        return false;
    }
    const QMimeData *md = clip->mimeData(QClipboard::Clipboard);
    if (!md)
    {
        return false;
    }

    auto tryBytes = [this](const QByteArray &bytes, QString mime) -> bool
    {
        if (bytes.isEmpty())
        {
            return false;
        }
        if (mime == QLatin1String("image/jpg"))
        {
            mime = QStringLiteral("image/jpeg");
        }
        if (!mime.startsWith(QLatin1String("image/")) || mime.contains(QLatin1String("svg")))
        {
            return false;
        }
        const QString ext = mime.section(QLatin1Char('/'), 1);
        return enqueueImageBytes(bytes, mime, QStringLiteral("pasted.%1").arg(ext));
    };
    auto tryImage = [this](const QImage &img) -> bool
    {
        if (img.isNull())
        {
            return false;
        }
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        if (!img.save(&buf, "PNG"))
        {
            return false;
        }
        return enqueueImageBytes(bytes, QStringLiteral("image/png"), QStringLiteral("pasted.png"));
    };

    if (md->hasImage() || md->hasFormat(QStringLiteral("application/x-qt-image")))
    {
        QImage img = qvariant_cast<QImage>(md->imageData());
        if (img.isNull())
        {
            img = clip->image(QClipboard::Clipboard);
        }
        if (tryImage(img))
        {
            return true;
        }
    }

    const QStringList fmts = md->formats();
    for (const QString &fmt : fmts)
    {
        if (!fmt.startsWith(QLatin1String("image/")) || fmt.contains(QLatin1String("svg")))
        {
            continue;
        }
        if (tryBytes(md->data(fmt), fmt))
        {
            return true;
        }
    }

    if (tryImage(clip->image(QClipboard::Clipboard)))
    {
        return true;
    }

    const QList<QUrl> urls = clipboardLocalUrls(md);
    if (!urls.isEmpty())
    {
        const int before = m_pending.size();
        for (const QUrl &u : urls)
        {
            const QString path = u.toLocalFile();
            const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
            if (!mime.startsWith(QLatin1String("image/")))
            {
                continue;
            }
            attachFile(path);
        }
        if (m_pending.size() > before)
        {
            return true;
        }
    }
    return false;
}

QList<QUrl> ChatController::clipboardLocalUrls(const QMimeData *md) const
{
    QList<QUrl> urls;
    if (!md)
    {
        return urls;
    }
    urls = md->urls();
    if (urls.isEmpty() && md->hasFormat(QStringLiteral("text/uri-list")))
    {
        const QString list = QString::fromUtf8(md->data(QStringLiteral("text/uri-list")));
        for (const QString &line : list.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('#')))
            {
                continue;
            }
            urls.append(QUrl(trimmed));
        }
    }
    QList<QUrl> local;
    for (const QUrl &u : urls)
    {
        if (u.isLocalFile())
        {
            local.append(u);
        }
    }
    return local;
}

bool ChatController::pasteClipboard()
{
    const QClipboard *clip = QGuiApplication::clipboard();
    if (!clip)
    {
        return false;
    }
    const QMimeData *md = clip->mimeData(QClipboard::Clipboard);
    if (!md)
    {
        return false;
    }

    const QList<QUrl> urls = clipboardLocalUrls(md);
    if (!urls.isEmpty())
    {
        QStringList errors;
        for (const QUrl &u : urls)
        {
            QString err;
            if (!tryAttachPath(u.toLocalFile(), &err) && !err.isEmpty())
            {
                errors.append(err);
            }
        }
        if (!errors.isEmpty())
        {
            setError(errors.join(QLatin1Char('\n')));
        }
        else
        {
            setError({});
        }
        return true;
    }

    if (pasteClipboardImage())
    {
        return true;
    }

    if (!md->hasText())
    {
        return false;
    }
    const QString t = md->text();
    if (t.size() < Attach::kPasteChipMin)
    {
        return false;
    }
    enqueuePaste(t);
    return true;
}

static bool isQmlDropArea(QObject *watched)
{
    return QByteArray(watched->metaObject()->className()).contains("DropArea");
}

static bool isTextInputItem(QObject *watched)
{
    const QByteArray cn = watched->metaObject()->className();
    return cn.contains("TextEdit") || cn.contains("TextInput") || cn.contains("TextArea");
}

bool ChatController::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_projects || m_projects->pane() != QLatin1String("chat"))
    {
        return QObject::eventFilter(watched, event);
    }
    const QEvent::Type type = event->type();
    if (type == QEvent::DragLeave)
    {
        if (qobject_cast<QWindow *>(watched) || isTextInputItem(watched))
        {
            setFileDropHover(false);
        }
        return false;
    }
    if (type != QEvent::DragEnter && type != QEvent::DragMove && type != QEvent::Drop)
    {
        return false;
    }
    auto *de = static_cast<QDropEvent *>(event);
    if (!de->mimeData() || !de->mimeData()->hasUrls())
    {
        return false;
    }
    bool anyLocal = false;
    for (const QUrl &u : de->mimeData()->urls())
    {
        if (u.isLocalFile())
        {
            anyLocal = true;
            break;
        }
    }
    if (!anyLocal)
    {
        return false;
    }
    // Let QML DropArea own drops on the chat pane; only intercept the composer
    // so TextArea does not insert file:// paths.
    if (isQmlDropArea(watched))
    {
        if (type == QEvent::DragEnter || type == QEvent::DragMove)
        {
            setFileDropHover(true);
        }
        else if (type == QEvent::Drop)
        {
            setFileDropHover(false);
        }
        return false;
    }
    if (type == QEvent::DragEnter || type == QEvent::DragMove)
    {
        setFileDropHover(true);
        if (isTextInputItem(watched))
        {
            de->acceptProposedAction();
            return true;
        }
        return false;
    }
    setFileDropHover(false);
    if (!isTextInputItem(watched))
    {
        return false;
    }
    QStringList errors;
    for (const QUrl &u : de->mimeData()->urls())
    {
        if (!u.isLocalFile())
        {
            continue;
        }
        QString err;
        if (!tryAttachPath(u.toLocalFile(), &err) && !err.isEmpty())
        {
            errors.append(err);
        }
    }
    if (!errors.isEmpty())
    {
        setError(errors.join(QLatin1Char('\n')));
    }
    else
    {
        setError({});
    }
    de->acceptProposedAction();
    return true;
}

void ChatController::removeAttachment(int index)
{
    if (index < 0 || index >= m_pending.size())
    {
        return;
    }
    m_pending.removeAt(index);
    emit pendingAttachmentsChanged();
}

void ChatController::send()
{
    if (m_streaming || m_compacting)
        return;
    QString text = m_composer.trimmed();
    const Compact::Command cmd = Compact::parseCommand(text);
    if (cmd.passthrough)
    {
        text = cmd.args;
    }
    else if (!cmd.name.isEmpty())
    {
        m_composer.clear();
        clearPending();
        emit composerTextChanged();
        if (cmd.name == QLatin1String("compact"))
        {
            startCompact(cmd.args, false);
        }
        else if (cmd.name == QLatin1String("help"))
        {
            setCompactStatus(Compact::helpText());
        }
        else if (cmd.name == QLatin1String("new"))
        {
            newChat();
            m_projects->showChat();
        }
        else if (cmd.name == QLatin1String("quit"))
        {
            QGuiApplication::quit();
        }
        else
        {
            setCompactStatus(Compact::unknownCommandText(cmd.name));
        }
        return;
    }
    m_suppressAutoCompact = false;
    QString extra;
    QVector<ContentPart> images;
    for (const PendingAttach &a : m_pending)
    {
        if (a.type == PendingAttach::Image)
        {
            images.append(a.image);
            continue;
        }
        if (a.type == PendingAttach::Paste)
        {
            QString body = a.paste;
            if (body.size() > 256 * 1024)
            {
                body.truncate(256 * 1024);
            }
            extra += QStringLiteral("\n\n%1:\n```\n%2\n```").arg(a.label, body);
            continue;
        }
        const QString path = a.path;
        QFileInfo fi(path);
        if (SpreadsheetExtract::isSpreadsheetPath(path))
        {
            QString err;
            const auto sheets = SpreadsheetExtract::extract(path, m_settings->officeBinaryPath(), &err);
            if (sheets.isEmpty())
            {
                extra += QStringLiteral("\n\nAttached spreadsheet `%1` could not be read: %2")
                             .arg(fi.fileName(), err.isEmpty() ? QStringLiteral("unknown error") : err);
                continue;
            }
            for (const SpreadsheetExtract::Sheet &sh : sheets)
            {
                QString csv = sh.csv;
                if (csv.size() > 32 * 1024)
                {
                    csv.truncate(32 * 1024);
                }
                extra += QStringLiteral("\n\nAttached spreadsheet `%1` sheet `%2`:\n```csv\n%3\n```")
                             .arg(fi.fileName(), sh.name, csv);
            }
            continue;
        }
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
        {
            continue;
        }
        extra += QStringLiteral("\n\nAttached file `%1`:\n```\n%2\n```")
                     .arg(fi.fileName(), QString::fromUtf8(f.read(32 * 1024)));
    }
    if (text.isEmpty() && extra.isEmpty() && images.isEmpty())
    {
        return;
    }

    ensureConversation();
    if (!m_private)
    {
        Conversation c = m_store->conversation(m_convId);
        if (c.title == QLatin1String("New chat") && !text.isEmpty())
        {
            c.title = truncateOneLine(text);
            c.updatedAt = nowMs();
            m_store->upsertConversation(c);
        }
    }

    ChatMessage user;
    user.id = newId();
    user.conversationId = m_convId;
    user.role = QStringLiteral("user");
    user.content = formatReplyPrompt(m_replyQuote, text) + extra;
    user.attachments = images;
    user.createdAt = nowMs();
    m_messages.append(user);

    Message stored;
    stored.id = user.id;
    stored.conversationId = m_convId;
    stored.role = user.role;
    stored.content = user.content;
    stored.createdAt = user.createdAt;
    persistMessage(stored);

    m_composer.clear();
    clearPending();
    emit composerTextChanged();
    clearReplyQuote();
    setError({});
    m_toolRounds = 0;
    m_forceFinalWrite = false;
    m_finalWriteAttempts = 0;
    startGeneration();
}

void ChatController::beginAssistant()
{
    ChatMessage a;
    a.id = newId();
    a.conversationId = m_convId;
    a.role = QStringLiteral("assistant");
    a.streaming = true;
    a.createdAt = nowMs();
    m_messages.append(a);
    m_accTools.clear();
}

QString ChatController::systemPrompt() const
{
    QString s = QString::fromUtf8(kSystem);
    if (m_settings->includeLocalTime())
        s += QLatin1Char('\n') + localTimePromptLine();
    if (m_settings->enableArtifacts())
        s += QString::fromUtf8(kArtifactInstructions);
    if (m_forceFinalWrite)
        s += QString::fromUtf8(kFinalWriteNudge);
    QString projectId = m_projects->currentProjectId();
    if (!m_convId.isEmpty() && !m_private)
    {
        const Conversation c = m_store->conversation(m_convId);
        if (!c.projectId.isEmpty())
        {
            projectId = c.projectId;
        }
    }
    const QString instr = m_projects->instructionsFor(projectId);
    if (!instr.isEmpty())
        s += QStringLiteral("\n\n# Project instructions\n") + instr;
    int inc = 0, trunc = 0;
    const QString files = m_projects->projectContextFor(projectId, &inc, &trunc);
    if (!files.isEmpty())
    {
        s += QStringLiteral("\n\n# Project files (%1 included").arg(inc);
        if (trunc)
            s += QStringLiteral(", %1 truncated").arg(trunc);
        s += QStringLiteral(")\n") + files;
    }
    return s;
}

QVector<ChatMessage> ChatController::apiHistory() const
{
    QVector<ChatMessage> out;
    ChatMessage sys;
    sys.role = QStringLiteral("system");
    sys.content = systemPrompt();
    out.append(sys);
    for (const ChatMessage &m : m_messages.all())
    {
        if (Compact::isCompactMessage(m))
        {
            sys.content += QStringLiteral("\n\n# Compacted earlier conversation\n") + m.content;
            out[0].content = sys.content;
            continue;
        }
        if (m.streaming && m.content.isEmpty() && m.toolCallsJson.isEmpty())
            continue;
        out.append(m);
    }
    for (int i = 0; i < out.size(); ++i)
    {
        if (out[i].role != QLatin1String("assistant") || out[i].toolCallsJson.isEmpty())
            continue;
        QSet<QString> ids;
        const QJsonDocument doc = QJsonDocument::fromJson(out[i].toolCallsJson.toUtf8());
        for (const auto &v : doc.array())
        {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                ids.insert(id);
        }
        for (int j = i + 1; j < out.size() && out[j].role == QLatin1String("tool"); ++j)
            ids.remove(out[j].toolCallId);
        if (!ids.isEmpty())
            out[i].toolCallsJson.clear();
    }
    if (m_forceFinalWrite)
    {
        ChatMessage nudge;
        nudge.role = QStringLiteral("user");
        nudge.content = QStringLiteral(
            "Write the final answer now using the tool results already in this conversation. "
            "If a page or report was requested, emit it in an <artifact> block. "
            "Do not call tools and do not emit <tool_call> XML.");
        out.append(nudge);
    }
    return out;
}

void ChatController::startGeneration()
{
    if (m_settings->currentModel().isEmpty())
    {
        setError(QStringLiteral("Select a model first. If the list is empty, pull one or fix the backend URL."));
        emit emptyHintChanged();
        return;
    }
    m_streaming = true;
    emit streamingChanged();
    beginAssistant();

    ChatRequest req;
    const Backend b = m_settings->currentBackend();
    req.baseUrl = b.baseUrl;
    req.apiKey = b.apiKey;
    req.model = m_settings->currentModel();
    req.messages = apiHistory();
    if (m_forceFinalWrite)
        req.tools = {};
    else
    {
        req.tools = m_settings->modelTools() ? m_mcp->host()->openaiTools() : QJsonArray{};
        if (m_settings->modelTools())
            req.tools.append(WebSearch::fetchToolDefinition());
        if (webSearchActive())
            req.tools.append(WebSearch::toolDefinition());
    }
    req.temperature = m_settings->temperature();
    req.topP = m_settings->topP();
    req.maxTokens = m_settings->maxTokens();
    req.contextSize = m_settings->contextSize();
    req.reasoningEffort = m_settings->modelThinking() ? m_thinkingMode : QString();
    m_client->streamChat(req);
}

void ChatController::onChunk(const QString &text)
{
    m_messages.appendAssistantDelta(text);
    const int used = m_contextUsed + estimateTokens(text);
    if (used != m_contextUsed)
    {
        m_contextUsed = used;
        emit contextUsageChanged();
    }
}

void ChatController::onReasoning(const QString &text)
{
    m_messages.appendReasoningDelta(text);
    const int used = m_contextUsed + estimateTokens(text);
    if (used != m_contextUsed)
    {
        m_contextUsed = used;
        emit contextUsageChanged();
    }
}

void ChatController::onToolDelta(int index, const QString &id, const QString &name, const QString &args)
{
    AccTool &t = m_accTools[index];
    if (!id.isEmpty())
        t.id = id;
    if (!name.isEmpty())
        t.name = name;
    t.arguments += args;
}

QJsonArray ChatController::assembledToolCalls() const
{
    QJsonArray arr;
    for (auto it = m_accTools.begin(); it != m_accTools.end(); ++it)
    {
        const AccTool &t = it.value();
        if (t.name.isEmpty())
            continue;
        arr.append(QJsonObject
        {
            {QStringLiteral("id"), t.id.isEmpty() ? QStringLiteral("call_%1").arg(it.key()) : t.id},
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"),
             QJsonObject{{QStringLiteral("name"), t.name},
                         {QStringLiteral("arguments"), t.arguments}}},
        });
    }
    return arr;
}

void ChatController::persistLastAssistant()
{
    if (m_private)
        return;
    Message stored = m_messages.last();
    if (stored.id.isEmpty())
        return;
    stored.conversationId = m_convId;
    if (!stored.createdAt)
        stored.createdAt = nowMs();
    persistMessage(stored);
    Conversation c = m_store->conversation(m_convId);
    c.updatedAt = nowMs();
    c.model = m_settings->currentModel();
    c.backendId = m_settings->currentBackendId();
    c.reasoningEffort = m_thinkingMode;
    m_store->upsertConversation(c);
}

void ChatController::extractArtifactsFrom(const ChatMessage &m)
{
    if (!m_settings->enableArtifacts())
        return;
    const auto drafts = ArtifactExtractor::extract(m.content);
    if (drafts.isEmpty())
        return;

    if (!m_private && !m_convId.isEmpty())
    {
        for (const ArtifactDraft &d : drafts)
        {
            Artifact a;
            a.id = newId();
            a.conversationId = m_convId;
            a.messageId = m.id;
            a.identifier = d.identifier;
            a.title = d.title;
            a.type = d.type;
            a.language = d.language;
            a.content = d.content;
            a.version = m_store->nextArtifactVersion(m_convId, d.identifier);
            a.createdAt = nowMs();
            m_store->insertArtifact(a);
        }
        loadArtifacts();
    }
    else
    {
        QMap<QString, Artifact> latest;
        for (int i = 0; i < m_artifacts.rowCount(); ++i)
        {
            const Artifact a = m_artifacts.at(i);
            latest.insert(a.identifier, a);
        }
        for (const ArtifactDraft &d : drafts)
        {
            Artifact a;
            a.id = newId();
            a.messageId = m.id;
            a.identifier = d.identifier;
            a.title = d.title;
            a.type = d.type;
            a.language = d.language;
            a.content = d.content;
            a.version = latest.contains(d.identifier) ? latest.value(d.identifier).version + 1 : 1;
            a.createdAt = nowMs();
            latest.insert(d.identifier, a);
        }
        applyArtifactItems(latest.values());
    }
    setArtifactPaneOpen(true);
    openArtifact(drafts.last().identifier);
}

void ChatController::applyArtifactItems(const QList<Artifact> &items)
{
    m_artifacts.setItems(items);
    m_artifactsByMessage.clear();
    for (const Artifact &a : items)
    {
        if (a.messageId.isEmpty())
            continue;
        QVariantMap row;
        row.insert(QStringLiteral("identifier"), a.identifier);
        row.insert(QStringLiteral("title"), a.title);
        row.insert(QStringLiteral("text"), a.content);
        m_artifactsByMessage[a.messageId].append(row);
    }
    syncCurrentArtifact();
    ++m_artifactsRevision;
    emit artifactsRevisionChanged();
}

void ChatController::loadArtifacts()
{
    if (m_private)
    {
        m_artifactsByMessage.clear();
        m_currentArtifact = {};
        writePreviewFile();
        ++m_artifactsRevision;
        emit artifactsRevisionChanged();
        return;
    }
    if (m_convId.isEmpty())
    {
        applyArtifactItems({});
        return;
    }
    const auto all = m_store->artifactsForConversation(m_convId);
    QMap<QString, Artifact> latest;
    for (const Artifact &a : all)
        latest.insert(a.identifier, a);
    applyArtifactItems(latest.values());
}

QVariantList ChatController::artifactsForMessage(const QString &messageId) const
{
    return m_artifactsByMessage.value(messageId);
}

Artifact ChatController::currentArtifact() const
{
    return m_currentArtifact;
}

void ChatController::syncCurrentArtifact()
{
    Artifact next;
    const Artifact latest = m_artifacts.at(m_artIndex);
    if (!latest.identifier.isEmpty())
    {
        const auto vers = m_store->artifactsForIdentifier(m_convId, latest.identifier);
        next = latest;
        for (const Artifact &a : vers)
        {
            if (a.version == m_artVersion)
            {
                next = a;
                break;
            }
        }
        if (vers.isEmpty())
            next = latest;
        else if (next.version != m_artVersion)
            next = vers.last();
    }
    m_currentArtifact = next;
    writePreviewFile();
}

QString ChatController::currentArtifactContent() const
{
    return currentArtifact().content;
}

QString ChatController::currentArtifactPreviewHtml() const
{
    const Artifact a = currentArtifact();
    if (HtmlDocument::isMarkdownType(a.type))
    {
        QTextDocument doc;
        doc.setMarkdown(a.content);
        return HtmlDocument::complete(doc.toHtml(), QStringLiteral("text/html"), a.title);
    }
    if (!currentArtifactHtml())
        return {};
    return HtmlDocument::complete(a.content, a.type, a.title);
}

QString ChatController::currentArtifactTitle() const
{
    return currentArtifact().title;
}

QString ChatController::currentArtifactType() const
{
    return currentArtifact().type;
}

bool ChatController::currentArtifactHtml() const
{
    const Artifact a = currentArtifact();
    return HtmlDocument::isHtmlType(a.type) || HtmlDocument::isSvgType(a.type)
        || HtmlDocument::looksLikeSvg(a.content);
}

bool ChatController::currentArtifactCanPreview() const
{
    return currentArtifactHtml() || HtmlDocument::isMarkdownType(currentArtifact().type);
}

bool ChatController::wordExportAvailable() const
{
    return currentArtifactCanPreview() && DocxExport::available(m_settings->officeBinaryPath());
}

QUrl ChatController::suggestedWordExportUrl() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::homePath();
    return QUrl::fromLocalFile(dir + QLatin1Char('/') + DocxExport::fileNameFromTitle(currentArtifactTitle()));
}

void ChatController::exportCurrentArtifactToWord(const QString &destPath)
{
    if (m_wordExportBusy)
        return;
    const QString html = currentArtifactPreviewHtml();
    if (html.isEmpty())
    {
        setError(QStringLiteral("This artifact has no HTML to export to Word."));
        return;
    }
    const QString override = m_settings->officeBinaryPath();
    if (!DocxExport::available(override))
    {
        if (override.trimmed().isEmpty())
            setError(QStringLiteral("LibreOffice or OpenOffice was not found. Install it, or set the soffice path in Settings → Advanced."));
        else
            setError(QStringLiteral("No usable LibreOffice or OpenOffice binary at %1.").arg(override.trimmed()));
        return;
    }
    QString dest = destPath;
    if (QUrl(dest).isLocalFile())
        dest = QUrl(dest).toLocalFile();
    m_wordExportBusy = true;
    emit wordExportBusyChanged();
    auto *thread = QThread::create([this, html, dest, override]()
    {
        const QString err = DocxExport::convertHtmlToDocx(html, dest, override);
        QMetaObject::invokeMethod(
            this,
            [this, err]()
            {
                onWordExportDone(err);
            },
            Qt::QueuedConnection);
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ChatController::onWordExportDone(const QString &err)
{
    m_wordExportBusy = false;
    emit wordExportBusyChanged();
    if (!err.isEmpty())
        setError(err);
}

void ChatController::writePreviewFile()
{
    const QString html = currentArtifactPreviewHtml();
    QUrl next;
    if (!html.isEmpty())
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/preview");
        QDir().mkpath(dir);
        m_previewPath = dir + QStringLiteral("/artifact.html");
        QFile f(m_previewPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            f.write(html.toUtf8());
            f.close();
            next = QUrl::fromLocalFile(m_previewPath);
        }
    }
    if (m_previewUrl == next)
        return;
    m_previewUrl = next;
    emit currentArtifactPreviewUrlChanged();
}

void ChatController::setCurrentArtifactIndex(int i)
{
    if (i < 0)
        i = 0;
    if (i >= m_artifacts.rowCount())
        i = qMax(0, m_artifacts.rowCount() - 1);
    m_artIndex = i;
    const Artifact a = m_artifacts.at(m_artIndex);
    const auto vers = m_store->artifactsForIdentifier(m_convId, a.identifier);
    m_artVersion = vers.isEmpty() ? 1 : vers.last().version;
    m_currentArtifact = vers.isEmpty() ? a : vers.last();
    emit currentArtifactIndexChanged();
    emit currentArtifactVersionChanged();
    writePreviewFile();
}

void ChatController::setCurrentArtifactVersion(int v)
{
    m_artVersion = v;
    emit currentArtifactVersionChanged();
    syncCurrentArtifact();
}

void ChatController::openArtifact(const QString &identifier)
{
    const int n = m_artifacts.rowCount();
    if (n <= 0)
        return;
    int found = -1;
    if (!identifier.isEmpty())
    {
        for (int i = 0; i < n; ++i)
        {
            const Artifact a = m_artifacts.at(i);
            if (a.identifier == identifier || a.title == identifier)
            {
                found = i;
                break;
            }
        }
    }
    if (found < 0)
        found = (m_artIndex >= 0 && m_artIndex < n) ? m_artIndex : 0;
    setCurrentArtifactIndex(found);
    setArtifactPaneOpen(true);
}

void ChatController::openCurrentArtifactInBrowser()
{
    const QString html = currentArtifactPreviewHtml();
    if (html.isEmpty())
    {
        setError(QStringLiteral("Nothing to open in a browser for this artifact."));
        return;
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::homePath();
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/shammy-preview.html");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        setError(f.errorString());
        return;
    }
    f.write(html.toUtf8());
    f.close();
    const QUrl url = QUrl::fromLocalFile(path);
    if (!QDesktopServices::openUrl(url))
        setError(QStringLiteral("Could not open a browser."));
}

void ChatController::saveCurrentArtifact(const QString &destPath)
{
    QString path = destPath;
    const QUrl u(destPath);
    if (u.isLocalFile())
        path = u.toLocalFile();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        setError(f.errorString());
        return;
    }
    const QByteArray bytes = currentArtifactHtml()
        ? currentArtifactPreviewHtml().toUtf8()
        : currentArtifactContent().toUtf8();
    f.write(bytes);
}

void ChatController::onFinished(const QString &reason)
{
    if (reason == QLatin1String("aborted"))
    {
        m_messages.finishLast();
        persistLastAssistant();
        m_forceFinalWrite = false;
        m_finalWriteAttempts = 0;
        setToolActivity({});
        m_streaming = false;
        emit streamingChanged();
        return;
    }
    m_messages.finishLast();
    QJsonArray calls = assembledToolCalls();
    if (calls.isEmpty())
        calls = ToolCallXml::parse(m_messages.last().content);

    const bool allowTools = !m_forceFinalWrite && m_toolRounds < 8;
    if (!calls.isEmpty() && allowTools)
    {
        m_messages.setLastToolCalls(QString::fromUtf8(QJsonDocument(calls).toJson(QJsonDocument::Compact)));
        persistLastAssistant();
        m_pendingToolQueue = calls;
        m_pendingToolI = 0;
        ++m_toolRounds;
        runPendingTools();
        return;
    }

    persistLastAssistant();
    m_forceFinalWrite = false;
    if (shouldForceFinalWrite(calls))
    {
        m_forceFinalWrite = true;
        ++m_finalWriteAttempts;
        startGeneration();
        return;
    }
    extractArtifactsFrom(m_messages.last());
    m_streaming = false;
    emit streamingChanged();
    emit emptyHintChanged();
    refreshContextUsage();
    maybeAutoCompact();
}

bool ChatController::shouldForceFinalWrite(const QJsonArray &leftoverCalls) const
{
    if (m_toolRounds <= 0 || m_finalWriteAttempts >= 2)
        return false;
    if (!leftoverCalls.isEmpty())
        return true;
    return looksLikeToolPreface(m_messages.last().content);
}

void ChatController::onFailed(const QString &err)
{
    m_messages.finishLast();
    persistLastAssistant();
    m_forceFinalWrite = false;
    m_finalWriteAttempts = 0;
    setError(err);
    setToolActivity({});
    m_streaming = false;
    emit streamingChanged();
}

void ChatController::stop()
{
    if (m_compacting)
    {
        m_client->abortComplete();
        finishCompacting();
        setCompactStatus(QStringLiteral("Compaction cancelled."));
        return;
    }
    if (!m_streaming)
        return;
    m_client->abort();
    if (!m_streaming)
        return;
    m_messages.finishLast();
    persistLastAssistant();
    setToolActivity({});
    m_streaming = false;
    emit streamingChanged();
}

void ChatController::setWebSearch(bool v)
{
    if (m_webSearch == v)
        return;
    m_webSearch = v;
    emit webSearchChanged();
}

bool ChatController::webSearchAvailable() const
{
    return m_settings && m_settings->webSearchReady() && m_settings->modelTools();
}

bool ChatController::webSearchActive() const
{
    return m_webSearch && webSearchAvailable();
}

void ChatController::setToolActivity(const QString &s)
{
    if (m_toolActivity == s)
        return;
    m_toolActivity = s;
    emit toolActivityChanged();
}

void ChatController::finishToolMessage(const QString &toolCallId, const QString &name, const QString &content)
{
    ChatMessage tm;
    tm.id = newId();
    tm.conversationId = m_convId;
    tm.role = QStringLiteral("tool");
    tm.toolCallId = toolCallId;
    tm.content = name.isEmpty() ? content : QStringLiteral("[%1]\n%2").arg(name, content);
    tm.createdAt = nowMs();
    m_messages.append(tm);
    persistMessage(tm);
    setToolActivity({});
    ++m_pendingToolI;
    runPendingTools();
}

void ChatController::runPendingTools()
{
    if (m_pendingToolI >= m_pendingToolQueue.size())
    {
        startGeneration();
        return;
    }
    executeOneTool();
}

void ChatController::executeOneTool()
{
    const QJsonObject call = m_pendingToolQueue.at(m_pendingToolI).toObject();
    const QString name = call.value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString();
    const QString argsStr = call.value(QStringLiteral("function")).toObject().value(QStringLiteral("arguments")).toString();
    const QString id = call.value(QStringLiteral("id")).toString();
    if (name == QLatin1String("web_search"))
    {
        QJsonParseError perr;
        const QJsonDocument adoc = QJsonDocument::fromJson(argsStr.toUtf8(), &perr);
        const QJsonObject args = adoc.isObject() ? adoc.object() : QJsonObject{};
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        QString activity = QStringLiteral("Searching the web…");
        if (!query.isEmpty())
        {
            QString q = query;
            if (q.size() > 72)
            {
                q.truncate(71);
                q += QChar(0x2026);
            }
            activity = QStringLiteral("Searching the web for “%1”…").arg(q);
        }
        setToolActivity(activity);
        m_web.search(m_settings->webSearchProvider(), m_settings->webSearchApiKey(), query,
                     [this, id](const QString &text, const QString &error)
                     {
                         finishToolMessage(id, QStringLiteral("web_search"),
                                           error.isEmpty() ? text : error);
                     });
        return;
    }
    if (name == QLatin1String("web_fetch"))
    {
        QJsonParseError perr;
        const QJsonDocument adoc = QJsonDocument::fromJson(argsStr.toUtf8(), &perr);
        const QJsonObject args = adoc.isObject() ? adoc.object() : QJsonObject{};
        const QString url = args.value(QStringLiteral("url")).toString().trimmed();
        QString activity = QStringLiteral("Fetching page…");
        const QString host = QUrl(url).host();
        if (!host.isEmpty())
            activity = QStringLiteral("Fetching %1…").arg(host);
        setToolActivity(activity);
        m_web.fetch(url, [this, id](const QString &text, const QString &error)
        {
            finishToolMessage(id, QStringLiteral("web_fetch"), error.isEmpty() ? text : error);
        });
        return;
    }
    const McpTool tool = m_mcp->host()->findTool(name);
    const QString key = tool.server + QLatin1Char('/') + tool.name;
    const bool allowed = m_sessionAllow.contains(key) || m_store->isAlwaysAllowed(tool.server, tool.name)
        || tool.name.isEmpty();
    if (!allowed && !tool.name.isEmpty())
    {
        m_permOpen = true;
        m_permTool = name;
        m_permServer = tool.server;
        m_permArgs = argsStr;
        m_permToolId = id;
        emit permissionChanged();
        return;
    }
    if (tool.name.isEmpty())
    {
        finishToolMessage(id, {}, QStringLiteral("unknown tool"));
        return;
    }
    QJsonParseError perr;
    const QJsonDocument adoc = QJsonDocument::fromJson(argsStr.toUtf8(), &perr);
    const QJsonObject args = adoc.isObject() ? adoc.object() : QJsonObject{};
    m_mcp->host()->callTool(name, args, [this, id, name](const QJsonValue &result, const QString &error)
    {
        QString content;
        if (!error.isEmpty())
            content = error;
        else if (result.isObject())
        {
            const QJsonArray arr = result.toObject().value(QStringLiteral("content")).toArray();
            QStringList bits;
            for (const auto &v : arr)
            {
                const QJsonObject o = v.toObject();
                bits << o.value(QStringLiteral("text")).toString();
            }
            content = bits.join(QLatin1Char('\n'));
            if (content.isEmpty())
                content = QString::fromUtf8(QJsonDocument(result.toObject()).toJson(QJsonDocument::Compact));
        }
        else
        {
            content = result.toString();
        }
        finishToolMessage(id, name, content);
    });
}

void ChatController::resolvePermission(const QString &decision)
{
    const McpTool tool = m_mcp->host()->findTool(m_permTool);
    const QString key = tool.server + QLatin1Char('/') + tool.name;
    m_permOpen = false;
    emit permissionChanged();
    if (decision == QLatin1String("deny"))
    {
        finishToolMessage(m_permToolId, {}, QStringLiteral("User denied tool call"));
        return;
    }
    if (decision == QLatin1String("chat") || decision == QLatin1String("once"))
        m_sessionAllow.insert(key);
    if (decision == QLatin1String("always"))
    {
        m_sessionAllow.insert(key);
        m_store->allowAlways(tool.server, tool.name);
    }
    executeOneTool();
}

void ChatController::regenerate()
{
    if (m_streaming || m_compacting || m_convId.isEmpty())
        return;
    auto all = m_messages.all();
    int lastUser = -1;
    for (int i = all.size() - 1; i >= 0; --i)
    {
        if (all[i].role == QLatin1String("user"))
        {
            lastUser = i;
            break;
        }
    }
    if (lastUser < 0)
        return;
    qint64 from = all[lastUser].createdAt;
    // keep the user message, drop after
    if (lastUser + 1 < all.size())
        from = all[lastUser + 1].createdAt;
    else
        return;
    m_store->deleteMessagesFrom(m_convId, from);
    openConversation(m_convId);
    m_toolRounds = 0;
    startGeneration();
}

void ChatController::editAndResend(const QString &messageId, const QString &newText)
{
    if (m_streaming || m_compacting)
        return;
    auto all = m_messages.all();
    int idx = -1;
    for (int i = 0; i < all.size(); ++i)
    {
        if (all[i].id == messageId)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return;
    m_store->deleteMessagesFrom(m_convId, all[idx].createdAt);
    openConversation(m_convId);
    m_composer = newText;
    emit composerTextChanged();
    send();
}

void ChatController::renameConversation(const QString &id, const QString &title)
{
    Conversation c = m_store->conversation(id);
    if (c.id.isEmpty())
        return;
    const QString next = title.trimmed();
    if (next.isEmpty() || next == c.title)
        return;
    c.title = next;
    c.updatedAt = nowMs();
    m_store->upsertConversation(c);
}

void ChatController::deleteConversation(const QString &id)
{
    m_store->deleteConversation(id);
    if (m_convId == id)
        newChat();
}

void ChatController::togglePin(const QString &id)
{
    Conversation c = m_store->conversation(id);
    if (c.id.isEmpty())
        return;
    c.pinned = !c.pinned;
    m_store->upsertConversation(c);
}

void ChatController::moveToProject(const QString &id, const QString &projectId)
{
    Conversation c = m_store->conversation(id);
    if (c.id.isEmpty())
    {
        return;
    }
    c.projectId = projectId;
    c.updatedAt = nowMs();
    m_store->upsertConversation(c);
}

void ChatController::setCompactStatus(const QString &s)
{
    if (m_compactStatus == s)
    {
        return;
    }
    m_compactStatus = s;
    emit compactStatusChanged();
}

void ChatController::finishCompacting()
{
    m_compacting = false;
    m_autoCompact = false;
    m_pendingPlan = {};
    m_compactConvId.clear();
    emit compactingChanged();
}

QVariantList ChatController::matchingSlashCommands(const QString &text) const
{
    QVariantList out;
    const auto cmds = Compact::matchingSlash(text);
    for (const Compact::SlashCommand &c : cmds)
    {
        QVariantMap m;
        m.insert(QStringLiteral("name"), c.name);
        m.insert(QStringLiteral("args"), c.args);
        m.insert(QStringLiteral("help"), c.help);
        out.append(m);
    }
    return out;
}

void ChatController::compact(const QString &extra)
{
    startCompact(extra, false);
}

void ChatController::maybeAutoCompact()
{
    if (m_suppressAutoCompact || m_compacting || m_streaming)
    {
        return;
    }
    const int lim = contextLimit();
    if (lim <= 0)
    {
        return;
    }
    if (m_contextUsed * 100 < lim * m_settings->compactionThreshold())
    {
        return;
    }
    startCompact({}, true);
}

void ChatController::startCompact(const QString &extra, bool automatic)
{
    if (m_streaming || m_compacting)
    {
        setCompactStatus(QStringLiteral("Wait for the current reply to finish."));
        return;
    }
    if (m_settings->currentModel().isEmpty())
    {
        setError(QStringLiteral("Select a model first."));
        return;
    }
    const int keepTurns = automatic ? Compact::kKeepUserTurns : 1;
    m_pendingPlan = Compact::plan(m_messages.all(), keepTurns);
    if (!m_pendingPlan.canCompact())
    {
        int userTurns = 0;
        for (const ChatMessage &m : m_messages.all())
        {
            if (m.role == QLatin1String("user"))
            {
                ++userTurns;
            }
        }
        if (userTurns < 2)
        {
            setCompactStatus(QStringLiteral(
                "Nothing to compact yet. Need at least two user turns so one can be summarized and the latest kept."));
        }
        else
        {
            setCompactStatus(QStringLiteral(
                "Nothing to compact — the latest exchange is already the whole chat."));
        }
        return;
    }
    ensureConversation();
    m_compactConvId = m_convId;
    m_usedBeforeCompact = m_contextUsed;
    m_autoCompact = automatic;
    m_compacting = true;
    emit compactingChanged();
    setCompactStatus(automatic ? QStringLiteral("Context threshold reached · compacting…")
                               : QStringLiteral("Compacting conversation…"));

    ChatRequest req;
    const Backend b = m_settings->currentBackend();
    req.baseUrl = b.baseUrl;
    req.apiKey = b.apiKey;
    req.model = m_settings->currentModel();
    req.messages = Compact::summaryRequest(Compact::transcript(m_pendingPlan.summarized), extra);
    req.temperature = 0.2;
    req.maxTokens = 2048;
    req.contextSize = m_settings->contextSize();
    req.stream = false;
    m_client->completeChat(req);
}

void ChatController::onCompactCompleted(const QString &text)
{
    if (!m_compacting)
    {
        return;
    }
    if (m_convId != m_compactConvId)
    {
        finishCompacting();
        return;
    }
    if (!m_private)
    {
        QStringList ids;
        for (const ChatMessage &m : m_pendingPlan.summarized)
        {
            if (!m.id.isEmpty())
                ids.append(m.id);
        }
        m_store->deleteMessages(ids);
    }
    ChatMessage sum;
    sum.id = newId();
    sum.conversationId = m_convId;
    sum.role = QStringLiteral("system");
    sum.content = Compact::storedSummaryBody(text);
    sum.createdAt = nowMs();
    persistMessage(sum);

    QVector<ChatMessage> next;
    next.append(sum);
    next += m_pendingPlan.tail;
    m_messages.setMessages(next);

    const int before = m_usedBeforeCompact;
    const int lim = contextLimit();
    m_contextUsed = -1;
    refreshContextUsage();
    const int after = m_contextUsed;
    const int pctBefore = (lim > 0) ? qRound(100.0 * double(before) / double(lim)) : 0;
    const int pctAfter = (lim > 0) ? qRound(100.0 * double(after) / double(lim)) : 0;
    m_suppressAutoCompact = m_autoCompact;
    finishCompacting();
    setCompactStatus(QStringLiteral("Compacted · %1 → %2 (%3% → %4%)")
                         .arg(formatUsageSize(before), formatUsageSize(after))
                         .arg(pctBefore)
                         .arg(pctAfter));
}

void ChatController::onCompactFailed(const QString &err)
{
    if (!m_compacting)
    {
        return;
    }
    finishCompacting();
    if (err == QLatin1String("aborted"))
    {
        setCompactStatus(QStringLiteral("Compaction cancelled."));
        return;
    }
    setError(QStringLiteral("Compaction failed: ") + err);
    setCompactStatus({});
}
