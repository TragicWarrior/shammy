#include "controllers/SettingsController.h"
#include "Util.h"
#include "artifacts/DocxExport.h"
#include "compact/Compactor.h"
#include "openai/ModelCaps.h"

#include <QDir>
#include <QSet>
#include <QUrl>
#include <QVariantMap>

SettingsController::SettingsController(Store *store, OpenAiClient *client, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_client(client)
{
    m_dark = m_qs.value(QStringLiteral("darkTheme"), true).toBool();
    m_showReasoning = m_qs.value(QStringLiteral("showReasoning"), true).toBool();
    m_showToolInsights = m_qs.value(QStringLiteral("showToolInsights"), false).toBool();
    m_showArtifactInsights = m_qs.value(QStringLiteral("showArtifactInsights"), false).toBool();
    m_rememberNav = m_qs.value(QStringLiteral("rememberNavigatorState"), false).toBool();
    m_navProjectsOpen = m_qs.value(QStringLiteral("navProjectsOpen"), false).toBool();
    m_navFavoritesOpen = m_qs.value(QStringLiteral("navFavoritesOpen"), false).toBool();
    m_navChatsOpen = m_qs.value(QStringLiteral("navChatsOpen"), false).toBool();
    m_enableArtifacts = m_qs.value(QStringLiteral("enableArtifacts"), true).toBool();
    m_officeBinaryPath = m_qs.value(QStringLiteral("officeBinaryPath")).toString().trimmed();
    m_compactionThreshold = Compact::clampThreshold(
        m_qs.value(QStringLiteral("compactionThreshold"), Compact::kDefaultThresholdPct).toInt());
    m_temperature = m_qs.value(QStringLiteral("temperature"), 0.7).toDouble();
    m_topP = m_qs.value(QStringLiteral("topP"), 1.0).toDouble();
    m_maxTokens = m_qs.value(QStringLiteral("maxTokens"), 0).toInt();
    m_model = m_qs.value(QStringLiteral("model")).toString();
    m_webSearchProvider = m_qs.value(QStringLiteral("webSearchProvider"), QStringLiteral("brave")).toString();
    if (m_webSearchProvider != QLatin1String("brave") && m_webSearchProvider != QLatin1String("tavily")
        && m_webSearchProvider != QLatin1String("exa"))
        m_webSearchProvider = QStringLiteral("brave");
    m_webSearchApiKey = m_qs.value(QStringLiteral("webSearchApiKey")).toString();
    if (m_qs.contains(QStringLiteral("webSearchEnabled")))
        m_webSearchEnabled = m_qs.value(QStringLiteral("webSearchEnabled")).toBool();
    else
        m_webSearchEnabled = !m_webSearchApiKey.trimmed().isEmpty();

    reloadBackends();
    m_backendId = m_store->setting(QStringLiteral("current_backend"));
    if (m_backendId.isEmpty() && !m_store->backends().isEmpty())
        m_backendId = m_store->backends().first().id;

    connect(m_store, &Store::backendsChanged, this, &SettingsController::reloadBackends);
    connect(m_client, &OpenAiClient::modelsListed, this,
            [this](const QStringList &ids, const QString &err)
            {
                m_loadingModels = false;
                emit loadingModelsChanged();
                m_modelsError = err;
                emit modelsErrorChanged();
                m_models.setIds(ids);
                if (!ids.isEmpty() && (m_model.isEmpty() || !ids.contains(m_model)))
                {
                    setCurrentModel(ids.first());
                }
                probeCurrentModel();
            });
    connect(m_client, &OpenAiClient::modelProbed, this, &SettingsController::onModelProbed);
    refreshModels();
}

void SettingsController::reloadBackends()
{
    m_backends.setItems(m_store->backends());
}

QString SettingsController::currentBackendName() const
{
    return currentBackend().name;
}

QString SettingsController::currentBaseUrl() const
{
    return currentBackend().baseUrl;
}

Backend SettingsController::currentBackend() const
{
    return m_store->backend(m_backendId);
}

void SettingsController::setCurrentBackendId(const QString &id)
{
    if (m_backendId == id)
        return;
    m_backendId = id;
    m_store->setSetting(QStringLiteral("current_backend"), id);
    emit currentBackendIdChanged();
    emit defaultThinkingModeChanged();
    emit contextSizeChanged();
    emit modelCapsChanged();
    refreshModels();
}

void SettingsController::setCurrentModel(const QString &m)
{
    if (m_model == m)
        return;
    m_model = m;
    m_qs.setValue(QStringLiteral("model"), m);
    emit currentModelChanged();
    emit defaultThinkingModeChanged();
    emit contextSizeChanged();
    emit modelCapsChanged();
    probeCurrentModel();
}

bool SettingsController::hasModel(const QString &name) const
{
    return !name.isEmpty() && m_models.ids().contains(name);
}

QString SettingsController::availableModel(const QString &preferred) const
{
    if (hasModel(preferred))
        return preferred;
    if (hasModel(m_model))
        return m_model;
    const QStringList ids = m_models.ids();
    return ids.isEmpty() ? QString() : ids.first();
}

void SettingsController::setTemperature(double v)
{
    m_temperature = v;
    m_qs.setValue(QStringLiteral("temperature"), v);
    emit samplingChanged();
}

void SettingsController::setTopP(double v)
{
    m_topP = v;
    m_qs.setValue(QStringLiteral("topP"), v);
    emit samplingChanged();
}

void SettingsController::setMaxTokens(int v)
{
    m_maxTokens = v;
    m_qs.setValue(QStringLiteral("maxTokens"), v);
    emit samplingChanged();
}

void SettingsController::setWebSearchEnabled(bool v)
{
    setBoolPref(&m_webSearchEnabled, QStringLiteral("webSearchEnabled"), v,
                &SettingsController::webSearchChanged);
}

void SettingsController::setWebSearchProvider(const QString &p)
{
    QString n = p.trimmed().toLower();
    if (n != QLatin1String("brave") && n != QLatin1String("tavily") && n != QLatin1String("exa"))
        n = QStringLiteral("brave");
    if (m_webSearchProvider == n)
        return;
    m_webSearchProvider = n;
    m_qs.setValue(QStringLiteral("webSearchProvider"), n);
    emit webSearchChanged();
}

void SettingsController::setWebSearchApiKey(const QString &k)
{
    if (m_webSearchApiKey == k)
        return;
    m_webSearchApiKey = k;
    m_qs.setValue(QStringLiteral("webSearchApiKey"), k);
    emit webSearchChanged();
}

bool SettingsController::webSearchReady() const
{
    return m_webSearchEnabled && !m_webSearchApiKey.trimmed().isEmpty();
}

QString SettingsController::reasoningKey() const
{
    return QStringLiteral("modelReasoning/%1/%2").arg(m_backendId, m_model);
}

static QString normalizeThinkDefault(QString mode)
{
    mode = mode.trimmed().toLower();
    if (mode == QLatin1String("low") || mode == QLatin1String("medium")
        || mode == QLatin1String("high"))
    {
        return mode;
    }
    return {};
}

QString SettingsController::thinkDefaultKey() const
{
    return QStringLiteral("modelThinkDefault/%1/%2").arg(m_backendId, m_model);
}

QString SettingsController::defaultThinkingMode() const
{
    if (m_backendId.isEmpty() || m_model.isEmpty())
    {
        return {};
    }
    if (m_qs.contains(thinkDefaultKey()))
    {
        return normalizeThinkDefault(m_qs.value(thinkDefaultKey()).toString());
    }
    if (m_qs.value(reasoningKey(), false).toBool())
    {
        return QStringLiteral("medium");
    }
    return {};
}

void SettingsController::setDefaultThinkingMode(const QString &mode)
{
    if (m_backendId.isEmpty() || m_model.isEmpty())
    {
        return;
    }
    const QString next = normalizeThinkDefault(mode);
    if (defaultThinkingMode() == next)
    {
        return;
    }
    m_qs.setValue(thinkDefaultKey(), next);
    emit defaultThinkingModeChanged();
}

QString SettingsController::reasoningEffort() const
{
    return defaultThinkingMode();
}

QString SettingsController::contextKey() const
{
    if (m_backendId.isEmpty() || m_model.isEmpty())
    {
        return QStringLiteral("modelContext/_default");
    }
    return QStringLiteral("modelContext/%1/%2").arg(m_backendId, m_model);
}

int SettingsController::parseContextSize(const QString &text)
{
    QString s = text.trimmed();
    s.remove(QLatin1Char(','));
    s.remove(QLatin1Char(' '));
    if (s.isEmpty())
    {
        return 16384;
    }
    int mul = 1;
    if (s.endsWith(QLatin1Char('k')) || s.endsWith(QLatin1Char('K')))
    {
        mul = 1024;
        s.chop(1);
    }
    else if (s.endsWith(QLatin1Char('m')) || s.endsWith(QLatin1Char('M')))
    {
        mul = 1024 * 1024;
        s.chop(1);
    }
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok || v <= 0)
    {
        return 16384;
    }
    int n = int(v * mul + 0.5);
    if (n < 512)
    {
        n = 512;
    }
    if (n > 1048576)
    {
        n = 1048576;
    }
    return n;
}

QString SettingsController::formatContextSize(int n)
{
    if (n >= 1024 && n % 1024 == 0)
    {
        return QString::number(n / 1024) + QLatin1Char('K');
    }
    return QString::number(n);
}

int SettingsController::contextSize() const
{
    const int n = m_qs.value(contextKey(), 16384).toInt();
    return n > 0 ? n : 16384;
}

void SettingsController::setContextSize(int n)
{
    if (n < 512)
    {
        n = 512;
    }
    if (n > 1048576)
    {
        n = 1048576;
    }
    if (contextSize() == n)
    {
        return;
    }
    m_qs.setValue(contextKey(), n);
    emit contextSizeChanged();
}

QString SettingsController::contextSizeLabel() const
{
    return formatContextSize(contextSize());
}

void SettingsController::setContextSizeFromText(const QString &text)
{
    setContextSize(parseContextSize(text));
}

void SettingsController::setBoolPref(bool *field, const QString &key, bool v,
                                    void (SettingsController::*sig)())
{
    if (*field == v)
        return;
    *field = v;
    m_qs.setValue(key, v);
    emit (this->*sig)();
}

void SettingsController::setDarkTheme(bool v)
{
    setBoolPref(&m_dark, QStringLiteral("darkTheme"), v, &SettingsController::darkThemeChanged);
}

void SettingsController::setShowReasoning(bool v)
{
    setBoolPref(&m_showReasoning, QStringLiteral("showReasoning"), v,
                &SettingsController::showReasoningChanged);
}

void SettingsController::setShowToolInsights(bool v)
{
    setBoolPref(&m_showToolInsights, QStringLiteral("showToolInsights"), v,
                &SettingsController::showToolInsightsChanged);
}

void SettingsController::setShowArtifactInsights(bool v)
{
    setBoolPref(&m_showArtifactInsights, QStringLiteral("showArtifactInsights"), v,
                &SettingsController::showArtifactInsightsChanged);
}

void SettingsController::setRememberNavigatorState(bool v)
{
    setBoolPref(&m_rememberNav, QStringLiteral("rememberNavigatorState"), v,
                &SettingsController::rememberNavigatorStateChanged);
}

void SettingsController::setNavProjectsOpen(bool v)
{
    setBoolPref(&m_navProjectsOpen, QStringLiteral("navProjectsOpen"), v,
                &SettingsController::navigatorStateChanged);
}

void SettingsController::setNavFavoritesOpen(bool v)
{
    setBoolPref(&m_navFavoritesOpen, QStringLiteral("navFavoritesOpen"), v,
                &SettingsController::navigatorStateChanged);
}

void SettingsController::setNavChatsOpen(bool v)
{
    setBoolPref(&m_navChatsOpen, QStringLiteral("navChatsOpen"), v,
                &SettingsController::navigatorStateChanged);
}

void SettingsController::setEnableArtifacts(bool v)
{
    setBoolPref(&m_enableArtifacts, QStringLiteral("enableArtifacts"), v,
                &SettingsController::enableArtifactsChanged);
}

void SettingsController::setOfficeBinaryPath(const QString &p)
{
    QString v = p.trimmed();
    if (v.startsWith(QLatin1String("file:")))
        v = QUrl(v).toLocalFile();
    if (v.startsWith(QLatin1Char('~'))
        && (v.size() == 1 || v.at(1) == QLatin1Char('/') || v.at(1) == QLatin1Char('\\')))
        v = QDir::homePath() + v.mid(1);
    if (m_officeBinaryPath == v)
        return;
    m_officeBinaryPath = v;
    m_qs.setValue(QStringLiteral("officeBinaryPath"), v);
    emit officeBinaryPathChanged();
}

QString SettingsController::officeDetectedPath() const
{
    return DocxExport::sofficePath({});
}

QString SettingsController::resolveOfficeBinary(const QString &overridePath) const
{
    return DocxExport::sofficePath(overridePath);
}

QString SettingsController::capUserKey(const QString &feat) const
{
    return QStringLiteral("modelCapsUser/%1/%2/%3").arg(m_backendId, m_model, feat);
}

QString SettingsController::capHintKey(const QString &backendId, const QString &model,
                                       const QString &feat) const
{
    return QStringLiteral("modelCapsHint/%1/%2/%3").arg(backendId, model, feat);
}

bool SettingsController::capHint(const QString &feat) const
{
    if (m_backendId.isEmpty() || m_model.isEmpty())
    {
        return false;
    }
    return m_qs.value(capHintKey(m_backendId, m_model, feat), false).toBool();
}

bool SettingsController::capEffective(const QString &feat) const
{
    if (m_backendId.isEmpty() || m_model.isEmpty())
    {
        return false;
    }
    if (m_qs.contains(capUserKey(feat)))
    {
        return m_qs.value(capUserKey(feat)).toBool();
    }
    return capHint(feat);
}

void SettingsController::setCapUser(const QString &feat, bool v)
{
    if (m_backendId.isEmpty() || m_model.isEmpty())
    {
        return;
    }
    if (capEffective(feat) == v)
    {
        return;
    }
    m_qs.setValue(capUserKey(feat), v);
    emit modelCapsChanged();
}

bool SettingsController::modelVision() const
{
    return capEffective(QStringLiteral("vision"));
}

void SettingsController::setModelVision(bool v)
{
    setCapUser(QStringLiteral("vision"), v);
}

bool SettingsController::modelTools() const
{
    return capEffective(QStringLiteral("tools"));
}

void SettingsController::setModelTools(bool v)
{
    setCapUser(QStringLiteral("tools"), v);
}

bool SettingsController::modelThinking() const
{
    return capEffective(QStringLiteral("thinking"));
}

void SettingsController::setModelThinking(bool v)
{
    setCapUser(QStringLiteral("thinking"), v);
}

bool SettingsController::modelAudio() const
{
    return capEffective(QStringLiteral("audio"));
}

void SettingsController::setModelAudio(bool v)
{
    setCapUser(QStringLiteral("audio"), v);
}

bool SettingsController::hintVision() const
{
    return capHint(QStringLiteral("vision"));
}

bool SettingsController::hintTools() const
{
    return capHint(QStringLiteral("tools"));
}

bool SettingsController::hintThinking() const
{
    return capHint(QStringLiteral("thinking"));
}

bool SettingsController::hintAudio() const
{
    return capHint(QStringLiteral("audio"));
}

bool SettingsController::modelCapsOverridden() const
{
    if (m_backendId.isEmpty() || m_model.isEmpty())
    {
        return false;
    }
    return m_qs.contains(capUserKey(QStringLiteral("vision")))
        || m_qs.contains(capUserKey(QStringLiteral("tools")))
        || m_qs.contains(capUserKey(QStringLiteral("thinking")))
        || m_qs.contains(capUserKey(QStringLiteral("audio")));
}

QString SettingsController::modelCapsSource() const
{
    if (m_backendId.isEmpty() || m_model.isEmpty())
    {
        return QStringLiteral("Select a model to set capabilities.");
    }
    if (modelCapsOverridden())
    {
        return QStringLiteral("Custom — overrides the detected hint for this model.");
    }
    const QString src =
        m_qs.value(QStringLiteral("modelCapsHintSource/%1/%2").arg(m_backendId, m_model)).toString();
    if (src == QLatin1String("ollama"))
    {
        return QStringLiteral("From Ollama /api/show. Toggle to override.");
    }
    if (src == QLatin1String("name"))
    {
        return QStringLiteral("Guessed from the model name. Toggle to override.");
    }
    return QStringLiteral("No advertisement yet — defaults off. Toggle to set, or wait for a probe.");
}

void SettingsController::resetModelCaps()
{
    if (m_backendId.isEmpty() || m_model.isEmpty())
    {
        return;
    }
    m_qs.remove(capUserKey(QStringLiteral("vision")));
    m_qs.remove(capUserKey(QStringLiteral("tools")));
    m_qs.remove(capUserKey(QStringLiteral("thinking")));
    m_qs.remove(capUserKey(QStringLiteral("audio")));
    emit modelCapsChanged();
}

void SettingsController::probeCurrentModel()
{
    const Backend b = currentBackend();
    if (b.baseUrl.isEmpty() || m_model.isEmpty())
    {
        return;
    }
    m_probeBackend = b.id;
    m_probeModel = m_model;
    const QString visionHint = capHintKey(b.id, m_model, QStringLiteral("vision"));
    if (!m_qs.contains(visionHint))
    {
        const ModelCaps guess = capsFromModelId(m_model);
        m_qs.setValue(visionHint, guess.vision);
        m_qs.setValue(capHintKey(b.id, m_model, QStringLiteral("tools")), guess.tools);
        m_qs.setValue(capHintKey(b.id, m_model, QStringLiteral("thinking")), guess.thinking);
        m_qs.setValue(capHintKey(b.id, m_model, QStringLiteral("audio")), guess.audio);
        m_qs.setValue(QStringLiteral("modelCapsHintSource/%1/%2").arg(b.id, m_model),
                      QStringLiteral("name"));
        emit modelCapsChanged();
    }
    m_client->probeModel(b.baseUrl, b.apiKey, m_model);
}

void SettingsController::onModelProbed(const QString &model, bool vision, bool tools, bool thinking,
                                       bool audio, bool advertised)
{
    if (model != m_probeModel)
    {
        return;
    }
    const QString backendId = m_probeBackend.isEmpty() ? m_backendId : m_probeBackend;
    if (backendId.isEmpty())
    {
        return;
    }
    m_qs.setValue(capHintKey(backendId, model, QStringLiteral("vision")), vision);
    m_qs.setValue(capHintKey(backendId, model, QStringLiteral("tools")), tools);
    m_qs.setValue(capHintKey(backendId, model, QStringLiteral("thinking")), thinking);
    m_qs.setValue(capHintKey(backendId, model, QStringLiteral("audio")), audio);
    m_qs.setValue(QStringLiteral("modelCapsHintSource/%1/%2").arg(backendId, model),
                  advertised ? QStringLiteral("ollama") : QStringLiteral("name"));
    if (backendId == m_backendId && model == m_model)
    {
        emit modelCapsChanged();
    }
}

void SettingsController::setCompactionThreshold(int percent)
{
    percent = Compact::clampThreshold(percent);
    if (m_compactionThreshold == percent)
    {
        return;
    }
    m_compactionThreshold = percent;
    m_qs.setValue(QStringLiteral("compactionThreshold"), percent);
    emit compactionThresholdChanged();
}

bool SettingsController::hasWebEngine() const
{
#ifdef SHAMMY_WEBENGINE
    return true;
#else
    return false;
#endif
}

void SettingsController::refreshModels()
{
    const Backend b = currentBackend();
    if (b.baseUrl.isEmpty())
    {
        m_models.setIds({});
        m_modelsError = QStringLiteral("No backend URL");
        emit modelsErrorChanged();
        return;
    }
    m_loadingModels = true;
    emit loadingModelsChanged();
    m_client->listModels(b.baseUrl, b.apiKey);
}

void SettingsController::saveBackend(const QString &id, const QString &name, const QString &url,
                                     const QString &apiKey)
{
    Backend b = m_store->backend(id);
    if (b.id.isEmpty())
    {
        b.id = id.isEmpty() ? newId() : id;
        b.createdAt = nowMs();
    }
    b.name = name;
    b.baseUrl = url;
    b.apiKey = apiKey;
    m_store->upsertBackend(b);
    if (m_backendId == b.id)
        refreshModels();
}

void SettingsController::addBackend()
{
    Backend b;
    b.id = newId();
    b.name = QStringLiteral("Custom");
    b.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
    b.createdAt = nowMs();
    m_store->upsertBackend(b);
}

void SettingsController::removeBackend(const QString &id)
{
    m_store->deleteBackend(id);
    if (m_backendId == id)
    {
        const auto all = m_store->backends();
        setCurrentBackendId(all.isEmpty() ? QString() : all.first().id);
    }
}

QString SettingsController::makeId() const
{
    return newId();
}

QVariantList SettingsController::backendSnapshot() const
{
    QVariantList out;
    for (const Backend &b : m_store->backends())
    {
        QVariantMap row;
        row.insert(QStringLiteral("backendId"), b.id);
        row.insert(QStringLiteral("name"), b.name);
        row.insert(QStringLiteral("baseUrl"), b.baseUrl);
        row.insert(QStringLiteral("apiKey"), b.apiKey);
        out.append(row);
    }
    return out;
}

void SettingsController::applyBackendSnapshot(const QVariantList &rows, const QString &activeId)
{
    QSet<QString> keep;
    for (const QVariant &v : rows)
    {
        const QVariantMap m = v.toMap();
        Backend b = m_store->backend(m.value(QStringLiteral("backendId")).toString());
        if (b.id.isEmpty())
        {
            b.id = m.value(QStringLiteral("backendId")).toString();
            if (b.id.isEmpty())
            {
                b.id = newId();
            }
            b.createdAt = nowMs();
        }
        b.name = m.value(QStringLiteral("name")).toString();
        b.baseUrl = m.value(QStringLiteral("baseUrl")).toString();
        b.apiKey = m.value(QStringLiteral("apiKey")).toString();
        keep.insert(b.id);
        m_store->upsertBackend(b);
    }
    const QList<Backend> existing = m_store->backends();
    for (const Backend &b : existing)
    {
        if (!keep.contains(b.id))
        {
            m_store->deleteBackend(b.id);
        }
    }
    QString active = activeId;
    if ((active.isEmpty() || !keep.contains(active)) && !rows.isEmpty())
    {
        active = rows.first().toMap().value(QStringLiteral("backendId")).toString();
    }
    if (!active.isEmpty())
    {
        setCurrentBackendId(active);
    }
    reloadBackends();
    refreshModels();
}
