#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

class SseParser
{
public:
    struct ToolCallDelta
    {
        int index = 0;
        QString id;
        QString name;
        QString argumentsDelta;
    };

    struct Event
    {
        QString contentDelta;
        QString reasoningDelta;
        QVector<ToolCallDelta> toolCalls;
        bool done = false;
        QString finishReason;
        QString error;
        bool hasUsage = false;
        int promptTokens = 0;
        int completionTokens = 0;
        int totalTokens = 0;

        bool isEmpty() const
        {
            return contentDelta.isEmpty() && reasoningDelta.isEmpty()
                && toolCalls.isEmpty() && !done && finishReason.isEmpty()
                && error.isEmpty() && !hasUsage;
        }
    };

    QVector<Event> feed(const QByteArray &data);
    void reset();

private:
    Event parsePayload(QByteArray payload) const;
    QByteArray m_buf;
};
