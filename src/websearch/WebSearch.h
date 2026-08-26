#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class WebSearch : public QObject
{
    Q_OBJECT
public:
    explicit WebSearch(QObject *parent = nullptr);

    static QJsonObject toolDefinition();
    static QJsonObject fetchToolDefinition();
    static QString formatBrave(const QJsonObject &body);
    static QString formatTavily(const QJsonObject &body);
    static QString formatExa(const QJsonObject &body);
    static bool urlAllowed(const QUrl &url);
    static QUrl canonicalizeFetchUrl(const QUrl &url);
    static QString extractText(const QByteArray &body, const QString &contentType);

    void search(const QString &provider, const QString &apiKey, const QString &query,
                const std::function<void(QString text, QString error)> &cb);
    void fetch(const QString &url, const std::function<void(QString text, QString error)> &cb);

private:
    QNetworkAccessManager m_nam;
};
