#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class JsonRpc
{
public:
    struct Message
    {
        bool valid = false;
        bool isResponse = false;
        bool isNotification = false;
        QJsonValue id;
        QString method;
        QJsonObject params;
        QJsonValue result;
        QJsonObject error;
        QString parseError;
    };

    static QByteArray encodeRequest(const QJsonValue &id, const QString &method,
                                    const QJsonObject &params = {});
    static QByteArray encodeNotification(const QString &method, const QJsonObject &params = {});
    static Message parseLine(const QByteArray &line);
};
