#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

// Agent skills: folders under ~/.config/shammy/skills/ (on macOS too), each
// holding a SKILL.md whose front matter names and describes it:
//
//   ---
//   name: sm-inventory
//   description: What it is for, and when to use it.
//   ---
//   The instructions, in Markdown.
//
// Only the names and descriptions go into the system prompt. The model loads a
// skill's instructions, or another file from its folder, with the load_skill
// tool when it decides the skill applies.
namespace Skills
{
struct Skill
{
    QString name;        // from the front matter, or the folder name
    QString description; // one paragraph, may be empty
    QString dir;         // absolute path of the skill's folder
};

// $XDG_CONFIG_HOME/shammy/skills, else ~/.config/shammy/skills.
QString defaultRoot();

// The skills under `root`, sorted by name. A folder without a SKILL.md is not
// a skill and is skipped.
QList<Skill> discover(const QString &root);

// Reads `name:` and `description:` from SKILL.md front matter. Exposed for tests.
void parseFrontMatter(const QString &text, QString *name, QString *description);

// The system prompt section listing `skills`; empty when there are none.
// `canRunCommands` says whether run_command is on, since most skills need it.
QString promptSection(const QList<Skill> &skills, bool canRunCommands);

// The load_skill tool, in OpenAI function format.
QJsonObject toolDefinition();

// What load_skill returns: SKILL.md (or `file`, relative to the skill's folder)
// with the folder's path and the names of its other files, cut to `maxBytes`.
// On failure, an empty string and *error worded for the model.
QString load(const QList<Skill> &skills, const QString &name, const QString &file, qint64 maxBytes,
             QString *error);
} // namespace Skills
