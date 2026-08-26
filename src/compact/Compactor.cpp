#include "compact/Compactor.h"

#include <QStringList>
#include <QtGlobal>

int Compact::clampThreshold(int percent)
{
    return qBound(kMinThresholdPct, percent, kMaxThresholdPct);
}

Compact::Command Compact::parseCommand(const QString &text)
{
    const QString t = text.trimmed();
    Command cmd;
    if (t.startsWith(QLatin1String("//")))
    {
        cmd.passthrough = true;
        cmd.args = t.mid(1);
        return cmd;
    }
    if (!t.startsWith(QLatin1Char('/')) || t.size() < 2)
    {
        return cmd;
    }
    const int sp = t.indexOf(QLatin1Char(' '));
    if (sp < 0)
    {
        cmd.name = t.mid(1).toLower();
    }
    else
    {
        cmd.name = t.mid(1, sp - 1).toLower();
        cmd.args = t.mid(sp + 1).trimmed();
    }
    if (cmd.name.isEmpty())
    {
        Command empty;
        return empty;
    }
    return cmd;
}

QVector<Compact::SlashCommand> Compact::slashCommands()
{
    return {
        {QStringLiteral("compact"), QStringLiteral("[notes]"),
         QStringLiteral("Summarize older turns to free context")},
        {QStringLiteral("help"), {}, QStringLiteral("List slash commands")},
        {QStringLiteral("new"), {}, QStringLiteral("Start a new chat")},
        {QStringLiteral("quit"), {}, QStringLiteral("Quit Shammy")},
    };
}

QVector<Compact::SlashCommand> Compact::matchingSlash(const QString &text)
{
    if (text.startsWith(QLatin1String("//")) || !text.startsWith(QLatin1Char('/')))
        return {};
    const int sp = text.indexOf(QLatin1Char(' '));
    if (sp >= 0)
        return {};
    const QString q = text.mid(1).toLower();
    QVector<SlashCommand> out;
    const auto all = slashCommands();
    for (const SlashCommand &c : all)
    {
        if (c.name.startsWith(q))
            out.append(c);
    }
    return out;
}

QString Compact::helpText()
{
    QStringList parts;
    const auto all = slashCommands();
    for (const SlashCommand &c : all)
    {
        QString s = QLatin1Char('/') + c.name;
        if (!c.args.isEmpty())
            s += QLatin1Char(' ') + c.args;
        parts.append(s);
    }
    return QStringLiteral("Commands: %1   (prefix // to send a slash as chat)")
        .arg(parts.join(QStringLiteral(" · ")));
}

QString Compact::unknownCommandText(const QString &name)
{
    return QStringLiteral("Unknown command /%1. Try /help.").arg(name);
}

bool Compact::isCompactMessage(const ChatMessage &m)
{
    return m.role == QLatin1String("system");
}

Compact::Plan Compact::plan(const QVector<ChatMessage> &msgs, int keepUserTurns)
{
    Plan p;
    if (keepUserTurns < 1)
    {
        keepUserTurns = 1;
    }
    int users = 0;
    int tailStart = -1;
    for (int i = msgs.size() - 1; i >= 0; --i)
    {
        if (msgs.at(i).role == QLatin1String("user"))
        {
            ++users;
            if (users == keepUserTurns)
            {
                tailStart = i;
                break;
            }
        }
    }
    if (tailStart <= 0)
    {
        return p;
    }
    for (int i = 0; i < tailStart; ++i)
    {
        p.summarized.append(msgs.at(i));
    }
    for (int i = tailStart; i < msgs.size(); ++i)
    {
        p.tail.append(msgs.at(i));
    }
    bool hasSubstance = false;
    for (const ChatMessage &m : p.summarized)
    {
        if (m.role == QLatin1String("user") || m.role == QLatin1String("assistant")
            || isCompactMessage(m))
        {
            if (!m.content.trimmed().isEmpty())
            {
                hasSubstance = true;
                break;
            }
        }
    }
    if (!hasSubstance)
    {
        p.summarized.clear();
        p.tail.clear();
    }
    return p;
}

static QString clip(const QString &s, int maxChars)
{
    if (s.size() <= maxChars)
    {
        return s;
    }
    const int head = maxChars * 2 / 3;
    const int tail = maxChars - head - 5;
    return s.left(head) + QStringLiteral("\n...\n") + s.right(qMax(0, tail));
}

QString Compact::transcript(const QVector<ChatMessage> &msgs, int maxChars)
{
    QString out;
    for (const ChatMessage &m : msgs)
    {
        QString body = m.content;
        if (!m.reasoning.isEmpty())
        {
            body += QStringLiteral("\n[reasoning] ") + m.reasoning;
        }
        if (body.trimmed().isEmpty())
        {
            continue;
        }
        out += m.role;
        out += QLatin1Char(':');
        out += QLatin1Char('\n');
        out += clip(body, 4000);
        out += QLatin1Char('\n');
        out += QLatin1Char('\n');
        if (out.size() >= maxChars)
        {
            out.truncate(maxChars);
            out += QStringLiteral("\n...");
            break;
        }
    }
    return out;
}

QVector<ChatMessage> Compact::summaryRequest(const QString &transcriptText, const QString &extra)
{
    ChatMessage sys;
    sys.role = QStringLiteral("system");
    sys.content = QStringLiteral(
        "You compact a chat transcript. Write a structured summary that preserves "
        "the user's goals, decisions, file paths, identifiers, constraints, "
        "unresolved tasks, and key facts. Do not continue the conversation, "
        "ask questions, or invent details. Output only the summary.");
    ChatMessage user;
    user.role = QStringLiteral("user");
    user.content = QStringLiteral("Summarize this earlier conversation:\n\n") + transcriptText;
    if (!extra.trimmed().isEmpty())
    {
        user.content += QStringLiteral("\n\nExtra instructions from the user:\n") + extra.trimmed();
    }
    return {sys, user};
}

QString Compact::storedSummaryBody(const QString &modelSummary)
{
    QString s = modelSummary.trimmed();
    if (s.isEmpty())
    {
        s = QStringLiteral("(empty summary)");
    }
    return s;
}
