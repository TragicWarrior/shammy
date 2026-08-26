#pragma once

#include "openai/ChatTypes.h"

#include <QString>
#include <QVector>

// Harness-side compaction: summarize older turns, keep a recent tail.
// The model API is not asked to compact; Shammy rewrites the transcript.
namespace Compact
{

inline constexpr int kMinThresholdPct = 20;
inline constexpr int kDefaultThresholdPct = 80;
inline constexpr int kMaxThresholdPct = 90;
inline constexpr int kKeepUserTurns = 2;
inline constexpr int kTranscriptMaxChars = 24000;

struct Command
{
    QString name;
    QString args;
    bool passthrough = false;
};

struct SlashCommand
{
    QString name;
    QString args;
    QString help;
};

struct Plan
{
    QVector<ChatMessage> summarized;
    QVector<ChatMessage> tail;

    bool canCompact() const
    {
        return !summarized.isEmpty() && !tail.isEmpty();
    }
};

int clampThreshold(int percent);
Command parseCommand(const QString &text);
QVector<SlashCommand> slashCommands();
QVector<SlashCommand> matchingSlash(const QString &text);
QString helpText();
QString unknownCommandText(const QString &name);
bool isCompactMessage(const ChatMessage &m);
Plan plan(const QVector<ChatMessage> &msgs, int keepUserTurns = kKeepUserTurns);
QString transcript(const QVector<ChatMessage> &msgs, int maxChars = kTranscriptMaxChars);
QVector<ChatMessage> summaryRequest(const QString &transcriptText, const QString &extra);
QString storedSummaryBody(const QString &modelSummary);

} // namespace Compact
