#pragma once

#include "controllers/McpController.h"
#include "controllers/ProjectController.h"
#include "controllers/SettingsController.h"
#include "models/ConversationListModel.h"
#include "models/MessageListModel.h"
#include "models/SimpleListModels.h"
#include "openai/ChatTypes.h"
#include "openai/OpenAiClient.h"
#include "persist/Store.h"
#include "compact/Compactor.h"
#include "websearch/WebSearch.h"

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariant>
#include <QVector>

class QEvent;
class QMimeData;

class ChatController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MessageListModel *messages READ messages CONSTANT)
    Q_PROPERTY(ConversationListModel *conversations READ conversations CONSTANT)
    Q_PROPERTY(ConversationListModel *favorites READ favorites CONSTANT)
    Q_PROPERTY(bool privateSession READ privateSession NOTIFY conversationChanged)
    Q_PROPERTY(ArtifactListModel *artifacts READ artifacts CONSTANT)
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(QString conversationId READ conversationId NOTIFY conversationChanged)
    Q_PROPERTY(QString conversationProjectName READ conversationProjectName NOTIFY conversationChanged)
    Q_PROPERTY(QString generatingConversationId READ generatingConversationId NOTIFY generatingConversationChanged)
    Q_PROPERTY(QString errorBanner READ errorBanner NOTIFY errorBannerChanged)
    Q_PROPERTY(QString composerText READ composerText WRITE setComposerText NOTIFY composerTextChanged)
    Q_PROPERTY(QString replyQuote READ replyQuote NOTIFY replyQuoteChanged)
    Q_PROPERTY(QStringList pendingAttachments READ pendingAttachments NOTIFY pendingAttachmentsChanged)
    Q_PROPERTY(bool fileDropHover READ fileDropHover NOTIFY fileDropHoverChanged)
    Q_PROPERTY(bool artifactPaneOpen READ artifactPaneOpen WRITE setArtifactPaneOpen NOTIFY artifactPaneOpenChanged)
    Q_PROPERTY(int currentArtifactIndex READ currentArtifactIndex WRITE setCurrentArtifactIndex NOTIFY currentArtifactIndexChanged)
    Q_PROPERTY(int currentArtifactVersion READ currentArtifactVersion WRITE setCurrentArtifactVersion NOTIFY currentArtifactVersionChanged)
    Q_PROPERTY(QString currentArtifactContent READ currentArtifactContent NOTIFY currentArtifactVersionChanged)
    Q_PROPERTY(QString currentArtifactPreviewHtml READ currentArtifactPreviewHtml NOTIFY currentArtifactVersionChanged)
    Q_PROPERTY(QUrl currentArtifactPreviewUrl READ currentArtifactPreviewUrl NOTIFY currentArtifactPreviewUrlChanged)
    Q_PROPERTY(QString currentArtifactTitle READ currentArtifactTitle NOTIFY currentArtifactVersionChanged)
    Q_PROPERTY(QString currentArtifactType READ currentArtifactType NOTIFY currentArtifactVersionChanged)
    Q_PROPERTY(bool currentArtifactHtml READ currentArtifactHtml NOTIFY currentArtifactVersionChanged)
    Q_PROPERTY(bool currentArtifactCanPreview READ currentArtifactCanPreview NOTIFY currentArtifactVersionChanged)
    Q_PROPERTY(bool wordExportAvailable READ wordExportAvailable NOTIFY wordExportAvailableChanged)
    Q_PROPERTY(bool wordExportBusy READ wordExportBusy NOTIFY wordExportBusyChanged)
    Q_PROPERTY(bool permissionOpen READ permissionOpen NOTIFY permissionChanged)
    Q_PROPERTY(QString permissionTool READ permissionTool NOTIFY permissionChanged)
    Q_PROPERTY(QString permissionServer READ permissionServer NOTIFY permissionChanged)
    Q_PROPERTY(QString permissionArgs READ permissionArgs NOTIFY permissionChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(QString emptyHint READ emptyHint NOTIFY emptyHintChanged)
    Q_PROPERTY(QString thinkingMode READ thinkingMode WRITE setThinkingMode NOTIFY thinkingModeChanged)
    Q_PROPERTY(QString thinkingModeLabel READ thinkingModeLabel NOTIFY thinkingModeChanged)
    Q_PROPERTY(int contextUsed READ contextUsed NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextLimit READ contextLimit NOTIFY contextUsageChanged)
    Q_PROPERTY(QString contextUsageLabel READ contextUsageLabel NOTIFY contextUsageChanged)
    Q_PROPERTY(bool compacting READ compacting NOTIFY compactingChanged)
    Q_PROPERTY(QString compactStatus READ compactStatus NOTIFY compactStatusChanged)
    Q_PROPERTY(bool webSearch READ webSearch WRITE setWebSearch NOTIFY webSearchChanged)
    Q_PROPERTY(bool webSearchAvailable READ webSearchAvailable NOTIFY webSearchAvailableChanged)
    Q_PROPERTY(QString toolActivity READ toolActivity NOTIFY toolActivityChanged)
    Q_PROPERTY(int artifactsRevision READ artifactsRevision NOTIFY artifactsRevisionChanged)
public:
    ChatController(Store *store, OpenAiClient *client, McpController *mcp,
                   ProjectController *projects, SettingsController *settings,
                   QObject *parent = nullptr);

    MessageListModel *messages() { return &m_messages; }
    ConversationListModel *conversations() { return &m_conversations; }
    ConversationListModel *favorites() { return &m_favorites; }
    bool privateSession() const { return m_private; }
    ArtifactListModel *artifacts() { return &m_artifacts; }
    int artifactsRevision() const { return m_artifactsRevision; }

    bool streaming() const { return m_streaming; }
    QString conversationId() const { return m_convId; }
    QString conversationProjectName() const;
    QString generatingConversationId() const { return m_genConvId; }
    QString errorBanner() const { return m_errorBanner; }
    QString composerText() const { return m_composer; }
    void setComposerText(const QString &t);
    QString replyQuote() const { return m_replyQuote; }
    QStringList pendingAttachments() const;
    bool fileDropHover() const { return m_fileDropHover; }
    bool artifactPaneOpen() const { return m_artifactPaneOpen; }
    void setArtifactPaneOpen(bool v);
    int currentArtifactIndex() const { return m_artIndex; }
    void setCurrentArtifactIndex(int i);
    int currentArtifactVersion() const { return m_artVersion; }
    void setCurrentArtifactVersion(int v);
    QString currentArtifactContent() const;
    QString currentArtifactPreviewHtml() const;
    QUrl currentArtifactPreviewUrl() const { return m_previewUrl; }
    QString currentArtifactTitle() const;
    QString currentArtifactType() const;
    bool currentArtifactHtml() const;
    bool currentArtifactCanPreview() const;
    bool wordExportAvailable() const;
    bool wordExportBusy() const { return m_wordExportBusy; }
    bool permissionOpen() const { return m_permOpen; }
    QString permissionTool() const { return m_permTool; }
    QString permissionServer() const { return m_permServer; }
    QString permissionArgs() const { return m_permArgs; }
    QString searchQuery() const { return m_search; }
    void setSearchQuery(const QString &q);
    QString emptyHint() const;
    QString thinkingMode() const { return m_thinkingMode; }
    void setThinkingMode(const QString &mode);
    QString thinkingModeLabel() const;
    int contextUsed() const { return m_contextUsed; }
    int contextLimit() const;
    QString contextUsageLabel() const;
    bool compacting() const { return m_compacting; }
    QString compactStatus() const { return m_compactStatus; }
    bool webSearch() const { return m_webSearch; }
    void setWebSearch(bool v);
    bool webSearchAvailable() const;
    QString toolActivity() const { return m_toolActivity; }

    Q_INVOKABLE void newChat();
    Q_INVOKABLE void newPrivateChat();
    Q_INVOKABLE void startProjectChat(const QString &text);
    Q_INVOKABLE void openConversation(const QString &id);
    Q_INVOKABLE void send();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void regenerate();
    Q_INVOKABLE void editAndResend(const QString &messageId, const QString &newText);
    Q_INVOKABLE void replyToSelection(const QString &text);
    Q_INVOKABLE void clearReplyQuote();
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void attachFile(const QString &urlOrPath);
    Q_INVOKABLE bool pasteClipboard();
    Q_INVOKABLE bool pasteClipboardImage();
    Q_INVOKABLE void removeAttachment(int index);
    Q_INVOKABLE void renameConversation(const QString &id, const QString &title);
    Q_INVOKABLE void deleteConversation(const QString &id);
    Q_INVOKABLE void togglePin(const QString &id);
    Q_INVOKABLE void moveToProject(const QString &id, const QString &projectId);
    Q_INVOKABLE void resolvePermission(const QString &decision);
    Q_INVOKABLE void openArtifact(const QString &identifier);
    Q_INVOKABLE QVariantList artifactsForMessage(const QString &messageId) const;
    Q_INVOKABLE void openCurrentArtifactInBrowser();
    Q_INVOKABLE void saveCurrentArtifact(const QString &destPath);
    Q_INVOKABLE void exportCurrentArtifactToWord(const QString &destPath);
    Q_INVOKABLE QUrl suggestedWordExportUrl() const;
    Q_INVOKABLE void reloadHistory();
    Q_INVOKABLE void compact(const QString &extra = {});
    Q_INVOKABLE QVariantList matchingSlashCommands(const QString &text) const;

signals:
    void streamingChanged();
    void conversationChanged();
    void generatingConversationChanged();
    void errorBannerChanged();
    void composerTextChanged();
    void replyQuoteChanged();
    void pendingAttachmentsChanged();
    void fileDropHoverChanged();
    void artifactPaneOpenChanged();
    void currentArtifactIndexChanged();
    void currentArtifactVersionChanged();
    void currentArtifactPreviewUrlChanged();
    void permissionChanged();
    void searchQueryChanged();
    void emptyHintChanged();
    void thinkingModeChanged();
    void contextUsageChanged();
    void compactingChanged();
    void compactStatusChanged();
    void webSearchChanged();
    void webSearchAvailableChanged();
    void toolActivityChanged();
    void artifactsRevisionChanged();
    void wordExportAvailableChanged();
    void wordExportBusyChanged();

private:
    struct AccTool
    {
        QString id;
        QString name;
        QString arguments;
    };

    void ensureConversation();
    void startGeneration();
    void onChunk(const QString &text);
    void onReasoning(const QString &text);
    void onToolDelta(int index, const QString &id, const QString &name, const QString &args);
    void onFinished(const QString &reason);
    void onFailed(const QString &err);
    void persistLastAssistant();
    void extractArtifactsFrom(const ChatMessage &m);
    void loadArtifacts();
    void applyArtifactItems(const QList<Artifact> &items);
    bool shouldForceFinalWrite(const QJsonArray &leftoverCalls) const;
    QVector<ChatMessage> apiHistory() const;
    QString systemPrompt() const;
    void beginAssistant();
    // Generation runs against a single "generating conversation" that may differ
    // from the visible one. While attached (generating == visible) these helpers
    // drive the live model; while detached they drive m_genMessages so the stream
    // keeps running in the background without touching what the user is viewing.
    void detachGeneration();
    void endGeneration();
    void genAppend(const ChatMessage &m);
    void genAppendAssistantDelta(const QString &text);
    void genAppendReasoningDelta(const QString &text);
    void genFinishLast();
    void genSetLastToolCalls(const QString &json);
    ChatMessage genLast() const;
    QVector<ChatMessage> genAllMessages() const;
    void runPendingTools();
    void executeOneTool();
    void finishToolMessage(const QString &toolCallId, const QString &name, const QString &content);
    bool webSearchActive() const;
    void setToolActivity(const QString &s);
    QJsonArray assembledToolCalls() const;
    Artifact currentArtifact() const;
    void syncCurrentArtifact();
    void writePreviewFile();
    void setError(const QString &s);
    void applyDefaultThinking();
    bool enqueueImageBytes(const QByteArray &bytes, const QString &mime, const QString &title);
    void persistThinkingMode();
    void persistMessage(const Message &m);
    void onWordExportDone(const QString &err);
    void beginSession(bool priv);
    void refreshContextUsage();
    void onUsage(int promptTokens, int completionTokens, int totalTokens);
    static int estimateTokens(const QString &text);
    void maybeAutoCompact();
    void startCompact(const QString &extra, bool automatic);
    void onCompactCompleted(const QString &text);
    void onCompactFailed(const QString &err);
    void finishCompacting();
    void setCompactStatus(const QString &s);
    bool eventFilter(QObject *watched, QEvent *event) override;
    void setFileDropHover(bool v);
    bool tryAttachPath(const QString &path, QString *error);
    void enqueuePaste(const QString &text);
    void clearPending();
    QList<QUrl> clipboardLocalUrls(const QMimeData *md) const;

    struct PendingAttach
    {
        enum Type
        {
            File,
            Image,
            Paste
        } type = File;
        QString label;
        QString rest;
        QString path;
        ContentPart image;
        QString paste;
    };

    Store *m_store = nullptr;
    OpenAiClient *m_client = nullptr;
    McpController *m_mcp = nullptr;
    ProjectController *m_projects = nullptr;
    SettingsController *m_settings = nullptr;
    WebSearch m_web;
    MessageListModel m_messages;
    ConversationListModel m_conversations;
    ConversationListModel m_favorites;
    ArtifactListModel m_artifacts;
    QHash<QString, QVariantList> m_artifactsByMessage;
    Artifact m_currentArtifact;
    int m_artifactsRevision = 0;
    QString m_convId;
    QString m_composer;
    QString m_replyQuote;
    QVector<PendingAttach> m_pending;
    bool m_fileDropHover = false;
    QString m_errorBanner;
    bool m_streaming = false;
    bool m_private = false;
    // Background generation context. m_genConvId is non-empty whenever a
    // generation is in flight (attached or not); m_genAttached is true when the
    // live model currently reflects it. m_genMessages holds the working message
    // list while detached; the model/backend/thinking are captured at start so a
    // detached tool loop keeps using the generating chat's settings.
    QString m_genConvId;
    bool m_genAttached = false;
    bool m_genPrivate = false;
    QString m_genModel;
    QString m_genBackendId;
    QString m_genThinking;
    QVector<ChatMessage> m_genMessages;
    bool m_webSearch = true;
    QString m_toolActivity;
    bool m_artifactPaneOpen = false;
    bool m_wordExportBusy = false;
    int m_artIndex = 0;
    int m_artVersion = 1;
    QString m_previewPath;
    QUrl m_previewUrl;
    QString m_search;
    int m_toolRounds = 0;
    bool m_forceFinalWrite = false;
    int m_finalWriteAttempts = 0;

    QMap<int, AccTool> m_accTools;
    QJsonArray m_pendingToolQueue;
    int m_pendingToolI = 0;
    bool m_permOpen = false;
    QString m_permTool;
    QString m_permServer;
    QString m_permArgs;
    QString m_permToolId;
    QSet<QString> m_sessionAllow;
    QString m_thinkingMode;
    int m_contextUsed = 0;
    int m_promptTokensEst = 0;
    bool m_compacting = false;
    bool m_autoCompact = false;
    bool m_suppressAutoCompact = false;
    QString m_compactStatus;
    QString m_compactConvId;
    Compact::Plan m_pendingPlan;
    int m_usedBeforeCompact = 0;
};
