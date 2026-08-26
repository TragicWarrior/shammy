#pragma once

#include "models/SimpleListModels.h"
#include "openai/OpenAiClient.h"
#include "persist/Store.h"

#include <QObject>
#include <QSettings>
#include <QVariantList>

class SettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BackendListModel *backends READ backends CONSTANT)
    Q_PROPERTY(ModelListModel *models READ models CONSTANT)
    Q_PROPERTY(QString currentBackendId READ currentBackendId WRITE setCurrentBackendId NOTIFY currentBackendIdChanged)
    Q_PROPERTY(QString currentModel READ currentModel WRITE setCurrentModel NOTIFY currentModelChanged)
    Q_PROPERTY(QString currentBackendName READ currentBackendName NOTIFY currentBackendIdChanged)
    Q_PROPERTY(QString currentBaseUrl READ currentBaseUrl NOTIFY currentBackendIdChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY samplingChanged)
    Q_PROPERTY(double topP READ topP WRITE setTopP NOTIFY samplingChanged)
    Q_PROPERTY(int maxTokens READ maxTokens WRITE setMaxTokens NOTIFY samplingChanged)
    Q_PROPERTY(QString defaultThinkingMode READ defaultThinkingMode WRITE setDefaultThinkingMode NOTIFY defaultThinkingModeChanged)
    Q_PROPERTY(int contextSize READ contextSize WRITE setContextSize NOTIFY contextSizeChanged)
    Q_PROPERTY(QString contextSizeLabel READ contextSizeLabel NOTIFY contextSizeChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY darkThemeChanged)
    Q_PROPERTY(bool showReasoning READ showReasoning WRITE setShowReasoning NOTIFY showReasoningChanged)
    Q_PROPERTY(bool showToolInsights READ showToolInsights WRITE setShowToolInsights NOTIFY showToolInsightsChanged)
    Q_PROPERTY(bool showArtifactInsights READ showArtifactInsights WRITE setShowArtifactInsights NOTIFY showArtifactInsightsChanged)
    Q_PROPERTY(bool rememberNavigatorState READ rememberNavigatorState WRITE setRememberNavigatorState NOTIFY rememberNavigatorStateChanged)
    Q_PROPERTY(bool navProjectsOpen READ navProjectsOpen WRITE setNavProjectsOpen NOTIFY navigatorStateChanged)
    Q_PROPERTY(bool navFavoritesOpen READ navFavoritesOpen WRITE setNavFavoritesOpen NOTIFY navigatorStateChanged)
    Q_PROPERTY(bool navChatsOpen READ navChatsOpen WRITE setNavChatsOpen NOTIFY navigatorStateChanged)
    Q_PROPERTY(bool enableArtifacts READ enableArtifacts WRITE setEnableArtifacts NOTIFY enableArtifactsChanged)
    Q_PROPERTY(QString officeBinaryPath READ officeBinaryPath WRITE setOfficeBinaryPath NOTIFY officeBinaryPathChanged)
    Q_PROPERTY(QString officeDetectedPath READ officeDetectedPath CONSTANT)
    Q_PROPERTY(int compactionThreshold READ compactionThreshold WRITE setCompactionThreshold NOTIFY compactionThresholdChanged)
    Q_PROPERTY(bool hasWebEngine READ hasWebEngine CONSTANT)
    Q_PROPERTY(QString modelsError READ modelsError NOTIFY modelsErrorChanged)
    Q_PROPERTY(bool loadingModels READ loadingModels NOTIFY loadingModelsChanged)
    Q_PROPERTY(bool modelVision READ modelVision WRITE setModelVision NOTIFY modelCapsChanged)
    Q_PROPERTY(bool modelTools READ modelTools WRITE setModelTools NOTIFY modelCapsChanged)
    Q_PROPERTY(bool modelThinking READ modelThinking WRITE setModelThinking NOTIFY modelCapsChanged)
    Q_PROPERTY(bool modelAudio READ modelAudio WRITE setModelAudio NOTIFY modelCapsChanged)
    Q_PROPERTY(QString modelCapsSource READ modelCapsSource NOTIFY modelCapsChanged)
    Q_PROPERTY(bool modelCapsOverridden READ modelCapsOverridden NOTIFY modelCapsChanged)
    Q_PROPERTY(bool webSearchEnabled READ webSearchEnabled WRITE setWebSearchEnabled NOTIFY webSearchChanged)
    Q_PROPERTY(QString webSearchProvider READ webSearchProvider WRITE setWebSearchProvider NOTIFY webSearchChanged)
    Q_PROPERTY(QString webSearchApiKey READ webSearchApiKey WRITE setWebSearchApiKey NOTIFY webSearchChanged)
    Q_PROPERTY(bool webSearchReady READ webSearchReady NOTIFY webSearchChanged)
public:
    SettingsController(Store *store, OpenAiClient *client, QObject *parent = nullptr);

    BackendListModel *backends() { return &m_backends; }
    ModelListModel *models() { return &m_models; }

    QString currentBackendId() const { return m_backendId; }
    void setCurrentBackendId(const QString &id);
    QString currentModel() const { return m_model; }
    void setCurrentModel(const QString &m);
    bool hasModel(const QString &name) const;
    QString availableModel(const QString &preferred = {}) const;
    QString currentBackendName() const;
    QString currentBaseUrl() const;
    Backend currentBackend() const;

    double temperature() const { return m_temperature; }
    void setTemperature(double v);
    double topP() const { return m_topP; }
    void setTopP(double v);
    int maxTokens() const { return m_maxTokens; }
    void setMaxTokens(int v);
    QString defaultThinkingMode() const;
    void setDefaultThinkingMode(const QString &mode);
    QString reasoningEffort() const;
    int contextSize() const;
    void setContextSize(int n);
    QString contextSizeLabel() const;
    Q_INVOKABLE void setContextSizeFromText(const QString &text);
    static int parseContextSize(const QString &text);
    static QString formatContextSize(int n);
    bool darkTheme() const { return m_dark; }
    void setDarkTheme(bool v);
    bool showReasoning() const { return m_showReasoning; }
    void setShowReasoning(bool v);
    bool showToolInsights() const { return m_showToolInsights; }
    void setShowToolInsights(bool v);
    bool showArtifactInsights() const { return m_showArtifactInsights; }
    void setShowArtifactInsights(bool v);
    bool rememberNavigatorState() const { return m_rememberNav; }
    void setRememberNavigatorState(bool v);
    bool navProjectsOpen() const { return m_navProjectsOpen; }
    void setNavProjectsOpen(bool v);
    bool navFavoritesOpen() const { return m_navFavoritesOpen; }
    void setNavFavoritesOpen(bool v);
    bool navChatsOpen() const { return m_navChatsOpen; }
    void setNavChatsOpen(bool v);
    bool enableArtifacts() const { return m_enableArtifacts; }
    void setEnableArtifacts(bool v);
    QString officeBinaryPath() const { return m_officeBinaryPath; }
    void setOfficeBinaryPath(const QString &p);
    QString officeDetectedPath() const;
    Q_INVOKABLE QString resolveOfficeBinary(const QString &overridePath) const;
    int compactionThreshold() const { return m_compactionThreshold; }
    void setCompactionThreshold(int percent);
    bool hasWebEngine() const;
    QString modelsError() const { return m_modelsError; }
    bool loadingModels() const { return m_loadingModels; }
    bool modelVision() const;
    void setModelVision(bool v);
    bool modelTools() const;
    void setModelTools(bool v);
    bool modelThinking() const;
    void setModelThinking(bool v);
    bool modelAudio() const;
    void setModelAudio(bool v);
    bool hintVision() const;
    bool hintTools() const;
    bool hintThinking() const;
    bool hintAudio() const;
    QString modelCapsSource() const;
    bool modelCapsOverridden() const;
    Q_INVOKABLE void resetModelCaps();
    bool webSearchEnabled() const { return m_webSearchEnabled; }
    void setWebSearchEnabled(bool v);
    QString webSearchProvider() const { return m_webSearchProvider; }
    void setWebSearchProvider(const QString &p);
    QString webSearchApiKey() const { return m_webSearchApiKey; }
    void setWebSearchApiKey(const QString &k);
    bool webSearchReady() const;

    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void saveBackend(const QString &id, const QString &name, const QString &url,
                                 const QString &apiKey);
    Q_INVOKABLE void addBackend();
    Q_INVOKABLE void removeBackend(const QString &id);
    Q_INVOKABLE QString makeId() const;
    Q_INVOKABLE QVariantList backendSnapshot() const;
    Q_INVOKABLE void applyBackendSnapshot(const QVariantList &rows, const QString &activeId);

signals:
    void currentBackendIdChanged();
    void currentModelChanged();
    void samplingChanged();
    void defaultThinkingModeChanged();
    void contextSizeChanged();
    void darkThemeChanged();
    void showReasoningChanged();
    void showToolInsightsChanged();
    void showArtifactInsightsChanged();
    void rememberNavigatorStateChanged();
    void navigatorStateChanged();
    void enableArtifactsChanged();
    void officeBinaryPathChanged();
    void compactionThresholdChanged();
    void modelsErrorChanged();
    void loadingModelsChanged();
    void modelCapsChanged();
    void webSearchChanged();

private:
    void reloadBackends();
    QString reasoningKey() const;
    QString thinkDefaultKey() const;
    QString contextKey() const;
    QString capUserKey(const QString &feat) const;
    QString capHintKey(const QString &backendId, const QString &model, const QString &feat) const;
    bool capEffective(const QString &feat) const;
    bool capHint(const QString &feat) const;
    void setCapUser(const QString &feat, bool v);
    void probeCurrentModel();
    void setBoolPref(bool *field, const QString &key, bool v, void (SettingsController::*sig)());
    void onModelProbed(const QString &model, bool vision, bool tools, bool thinking, bool audio,
                       bool advertised);
    Store *m_store = nullptr;
    OpenAiClient *m_client = nullptr;
    BackendListModel m_backends;
    ModelListModel m_models;
    QString m_backendId;
    QString m_model;
    double m_temperature = 0.7;
    double m_topP = 1.0;
    int m_maxTokens = 0;
    bool m_dark = true;
    bool m_showReasoning = true;
    bool m_showToolInsights = false;
    bool m_showArtifactInsights = false;
    bool m_rememberNav = false;
    bool m_navProjectsOpen = false;
    bool m_navFavoritesOpen = false;
    bool m_navChatsOpen = false;
    bool m_enableArtifacts = true;
    QString m_officeBinaryPath;
    int m_compactionThreshold = 80;
    QString m_modelsError;
    bool m_loadingModels = false;
    QString m_probeBackend;
    QString m_probeModel;
    QSettings m_qs;
    bool m_webSearchEnabled = false;
    QString m_webSearchProvider = QStringLiteral("brave");
    QString m_webSearchApiKey;
};
