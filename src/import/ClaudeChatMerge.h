#pragma once

#include <QJsonObject>
#include <QString>

class Store;

// Bringing one Claude conversation into the local store, in a way that is safe
// to repeat. A chat that was imported before is updated, not replaced: the
// messages that came from Claude are refreshed, and anything added to the chat
// here (follow-up messages, its title, model, pin) is left alone.
namespace ClaudeChatMerge
{
struct Outcome
{
    QString conversationId; // empty if the JSON had nothing usable
    bool created = false;   // false: an existing chat was updated
    int messages = 0;       // messages taken from Claude
};

// `chat` is Claude's conversation object (with chat_messages); `meta` its entry
// in the project's conversation list. `model` is given to a chat that is new.
Outcome merge(Store *store, const QString &projectId, const QString &model, const QJsonObject &chat,
              const QJsonObject &meta = {});
} // namespace ClaudeChatMerge
