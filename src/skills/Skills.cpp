#include "skills/Skills.h"
#include "artifacts/FileImport.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace Skills
{
namespace
{
constexpr int kMaxDescription = 1024; // characters, as other harnesses allow
constexpr int kMaxListedFiles = 50;
constexpr qint64 kMaxSkillFile = 1024 * 1024; // larger files are not read at all

QString readText(const QString &path, qint64 limit, bool *binary = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.read(limit);
    if (binary)
        *binary = data.left(8192).contains('\0');
    return QString::fromUtf8(data);
}

// A YAML scalar as written after "key:", without its quotes.
QString unquote(QString v)
{
    v = v.trimmed();
    if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
    {
        v = v.mid(1, v.size() - 2);
        v.replace(QLatin1String("\\\""), QLatin1String("\""));
        v.replace(QLatin1String("\\\\"), QLatin1String("\\"));
        return v;
    }
    if (v.size() >= 2 && v.startsWith(QLatin1Char('\'')) && v.endsWith(QLatin1Char('\'')))
    {
        v = v.mid(1, v.size() - 2);
        v.replace(QLatin1String("''"), QLatin1String("'"));
    }
    return v;
}

bool validName(const QString &n)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,63}$"));
    return re.match(n).hasMatch();
}

const Skill *find(const QList<Skill> &skills, const QString &name)
{
    const QString want = name.trimmed().toLower();
    for (const Skill &s : skills)
    {
        if (s.name.toLower() == want || QFileInfo(s.dir).fileName().toLower() == want)
            return &s;
    }
    return nullptr;
}

QStringList otherFiles(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir base(dir);
    while (it.hasNext())
    {
        const QString rel = base.relativeFilePath(it.next());
        if (rel != QLatin1String("SKILL.md") && !rel.startsWith(QLatin1Char('.')))
            out << rel;
    }
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

// $XDG_CONFIG_HOME/shammy/skills, else ~/.config/shammy/skills.
static QString xdgRoot()
{
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base))
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/shammy/skills");
}

QString defaultRoot()
{
#if defined(Q_OS_MACOS)
    // Lowercase, like the folder Qt makes for Shammy's data: a case-sensitive
    // disk would not find "Shammy".
    return QDir::homePath() + QStringLiteral("/Library/Application Support/shammy/skills");
#else
    return xdgRoot();
#endif
}

QStringList roots()
{
#if defined(Q_OS_MACOS)
    // Mac tools keep their settings in either place, so the XDG folder is read too.
    return {defaultRoot(), xdgRoot()};
#else
    return {defaultRoot()};
#endif
}

void parseFrontMatter(const QString &text, QString *name, QString *description)
{
    name->clear();
    description->clear();
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &l : lines)
    {
        if (l.endsWith(QLatin1Char('\r')))
            l.chop(1);
    }
    int i = 0;
    while (i < lines.size() && lines.at(i).trimmed().isEmpty())
        ++i;
    if (i >= lines.size() || lines.at(i).trimmed() != QLatin1String("---"))
        return;
    ++i;
    while (i < lines.size() && lines.at(i).trimmed() != QLatin1String("---"))
    {
        const QString line = lines.at(i++);
        if (line.isEmpty() || line.at(0).isSpace() || line.startsWith(QLatin1Char('#')))
            continue;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed();
        QString value = line.mid(colon + 1).trimmed();
        // Block scalars ("|", ">") and plain values continued on indented lines.
        const bool literal = value.startsWith(QLatin1Char('|'));
        const bool folded = value.startsWith(QLatin1Char('>'));
        QStringList more;
        while (i < lines.size() && lines.at(i).trimmed() != QLatin1String("---")
               && (lines.at(i).isEmpty() || lines.at(i).at(0).isSpace()))
        {
            more << lines.at(i++).trimmed();
        }
        if (literal || folded)
            value = more.join(literal ? QStringLiteral("\n") : QStringLiteral(" ")).trimmed();
        else if (!more.isEmpty() && !value.isEmpty())
            value = (value + QLatin1Char(' ') + more.join(QLatin1Char(' '))).trimmed();
        value = unquote(value);
        if (key == QLatin1String("name"))
            *name = value;
        else if (key == QLatin1String("description"))
            *description = value.simplified();
    }
}

QList<Skill> discover(const QString &root)
{
    QList<Skill> out;
    const QDir dir(root);
    const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &d : subdirs)
    {
        const QString file = d.absoluteFilePath() + QStringLiteral("/SKILL.md");
        if (!QFileInfo(file).isFile())
            continue;
        Skill s;
        s.dir = d.absoluteFilePath();
        parseFrontMatter(readText(file, 64 * 1024), &s.name, &s.description);
        if (!validName(s.name))
            s.name = d.fileName();
        if (s.description.size() > kMaxDescription)
            s.description = s.description.left(kMaxDescription - 1) + QChar(0x2026);
        out << s;
    }
    std::sort(out.begin(), out.end(), [](const Skill &a, const Skill &b) { return a.name < b.name; });
    return out;
}

QList<Skill> discover(const QStringList &roots)
{
    QList<Skill> out;
    QSet<QString> seen;
    for (const QString &root : roots)
    {
        // A name already found in an earlier root hides this one.
        for (const Skill &s : discover(root))
        {
            if (!seen.contains(s.name))
            {
                seen.insert(s.name);
                out << s;
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const Skill &a, const Skill &b) { return a.name < b.name; });
    return out;
}

QString promptSection(const QList<Skill> &skills, bool canRunCommands)
{
    if (skills.isEmpty())
        return {};
    QString s = QStringLiteral(
        "\n\n# Skills\n"
        "These skills hold instructions for particular tasks. When a request matches one, call load_skill "
        "with its name and follow what it says before answering. Load only the skills you need.\n");
    if (!canRunCommands)
    {
        s += QStringLiteral(
            "Running commands is turned off. If a skill needs to run one, say so, and tell the user it can be "
            "turned on in Settings → Skills.\n");
    }
    for (const Skill &k : skills)
        s += QStringLiteral("- %1: %2\n").arg(k.name, k.description.isEmpty() ? QStringLiteral("(no description)") : k.description);
    return s;
}

QJsonObject toolDefinition()
{
    QJsonObject props;
    props.insert(QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                     {QStringLiteral("description"), QStringLiteral("The skill's name, as listed.")}});
    props.insert(QStringLiteral("file"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                             {QStringLiteral("description"),
                              QStringLiteral("Optional: another file from the skill's folder, as a relative path. "
                                             "Leave out to load the skill's instructions.")}});
    QJsonObject params{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), props},
                       {QStringLiteral("required"), QJsonArray{QStringLiteral("name")}}};
    QJsonObject fn{{QStringLiteral("name"), QStringLiteral("load_skill")},
                   {QStringLiteral("description"),
                    QStringLiteral("Load the instructions of a skill listed in the system prompt, or another file "
                                   "from that skill's folder.")},
                   {QStringLiteral("parameters"), params}};
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("function")}, {QStringLiteral("function"), fn}};
}

QString load(const QList<Skill> &skills, const QString &name, const QString &file, qint64 maxBytes, QString *error)
{
    const Skill *s = find(skills, name);
    if (!s)
    {
        QStringList names;
        for (const Skill &k : skills)
            names << k.name;
        *error = names.isEmpty() ? QStringLiteral("There are no skills installed.")
                                 : QStringLiteral("No skill named \"%1\". Installed: %2.").arg(name.trimmed(), names.join(QStringLiteral(", ")));
        return {};
    }
    const QString rel = file.trimmed().isEmpty() ? QStringLiteral("SKILL.md") : file.trimmed();
    // Only files inside the skill's own folder, also after following links.
    const QString root = QFileInfo(s->dir).canonicalFilePath();
    const QString path = QFileInfo(QDir(s->dir).filePath(rel)).canonicalFilePath();
    if (QDir::isAbsolutePath(rel) || path.isEmpty() || root.isEmpty() || !path.startsWith(root + QLatin1Char('/')))
    {
        *error = QStringLiteral("No file \"%1\" in the %2 skill's folder.").arg(rel, s->name);
        return {};
    }
    if (!QFileInfo(path).isFile())
    {
        *error = QStringLiteral("\"%1\" in the %2 skill's folder is not a file.").arg(rel, s->name);
        return {};
    }
    if (QFileInfo(path).size() > kMaxSkillFile)
    {
        *error = QStringLiteral("\"%1\" is too large to load (over 1 MB).").arg(rel);
        return {};
    }
    bool binary = false;
    QByteArray body = readText(path, kMaxSkillFile, &binary).toUtf8();
    if (binary)
    {
        *error = QStringLiteral("\"%1\" is not a text file.").arg(rel);
        return {};
    }

    QString head = QStringLiteral("Skill: %1\nFolder: %2\n").arg(s->name, s->dir);
    if (rel == QLatin1String("SKILL.md"))
    {
        head += QStringLiteral("Paths in these instructions are relative to that folder.\n");
        QStringList others = otherFiles(s->dir);
        if (!others.isEmpty())
        {
            const int total = others.size();
            others = others.mid(0, kMaxListedFiles);
            head += QStringLiteral("Other files (load one with the file argument): %1%2\n")
                        .arg(others.join(QStringLiteral(", ")),
                             total > kMaxListedFiles ? QStringLiteral(", and %1 more").arg(total - kMaxListedFiles) : QString());
        }
    }
    else
    {
        head += QStringLiteral("File: %1\n").arg(rel);
    }
    head += QLatin1Char('\n');
    const QByteArray headBytes = head.toUtf8();
    const bool cut = FileImport::cutToFit(body, qMax<qint64>(0, maxBytes - headBytes.size()));
    QString out = head + QString::fromUtf8(body);
    if (cut)
        out += QStringLiteral("\n\n(Cut to fit the model's context window.)");
    return out;
}
} // namespace Skills
