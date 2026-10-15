#include "skills/Skills.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class TestSkills : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString root() const { return m_dir.filePath(QStringLiteral("skills")); }
    bool write(const QString &rel, const QByteArray &data)
    {
        const QString path = root() + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(write(QStringLiteral("sm-inventory/SKILL.md"),
                      "---\nname: sm-inventory\ndescription: Query the store's inventory.\nmetadata: {\"x\": 1}\n---\n"
                      "# Inventory\nRun scripts/stock.sh.\n"));
        QVERIFY(write(QStringLiteral("sm-inventory/scripts/stock.sh"), "#!/bin/sh\necho stock\n"));
        QVERIFY(write(QStringLiteral("sm-inventory/references/api.md"), "API notes\n"));
        QVERIFY(write(QStringLiteral("sm-inventory/logo.png"), QByteArray("\x89PNG\0\0\0", 7)));
        // Folded description, quoted name that is not a valid identifier: the folder name is used.
        QVERIFY(write(QStringLiteral("weather/SKILL.md"),
                      "---\nname: \"Weather Thing\"\ndescription: >\n  Look up the forecast\n  for a city.\n---\nUse curl.\n"));
        // No front matter at all.
        QVERIFY(write(QStringLiteral("bare/SKILL.md"), "Just instructions.\n"));
        // Not a skill: no SKILL.md.
        QVERIFY(write(QStringLiteral("notes/README.md"), "nothing\n"));
        QVERIFY(write(QStringLiteral("secret.txt"), "outside every skill\n"));
    }

    void defaultRootFollowsXdg()
    {
        const QByteArray old = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", "/tmp/xdg-test");
#if defined(Q_OS_MACOS)
        // A Mac keeps skills beside config.json first, and reads the XDG folder after it.
        QCOMPARE(Skills::defaultRoot(),
                 QDir::homePath() + QStringLiteral("/Library/Application Support/Shammy/skills"));
        QCOMPARE(Skills::roots(), (QStringList{Skills::defaultRoot(), QStringLiteral("/tmp/xdg-test/shammy/skills")}));
        qunsetenv("XDG_CONFIG_HOME");
        QCOMPARE(Skills::roots().value(1), QDir::homePath() + QStringLiteral("/.config/shammy/skills"));
        if (!old.isEmpty())
            qputenv("XDG_CONFIG_HOME", old);
        return;
#endif
        QCOMPARE(Skills::defaultRoot(), QStringLiteral("/tmp/xdg-test/shammy/skills"));
        QCOMPARE(Skills::roots(), QStringList{Skills::defaultRoot()});
        qputenv("XDG_CONFIG_HOME", "relative/ignored");
        QCOMPARE(Skills::defaultRoot(), QDir::homePath() + QStringLiteral("/.config/shammy/skills"));
        qunsetenv("XDG_CONFIG_HOME");
        QCOMPARE(Skills::defaultRoot(), QDir::homePath() + QStringLiteral("/.config/shammy/skills"));
        if (!old.isEmpty())
            qputenv("XDG_CONFIG_HOME", old);
    }

    void frontMatterForms()
    {
        QString name, desc;
        Skills::parseFrontMatter(QStringLiteral("---\r\nname: a\r\ndescription: 'It''s here'\r\n---\r\nbody"), &name, &desc);
        QCOMPARE(name, QStringLiteral("a"));
        QCOMPARE(desc, QStringLiteral("It's here"));
        Skills::parseFrontMatter(QStringLiteral("---\nname: b\ndescription: |\n  line one\n  line two\nlicense: MIT\n---\n"), &name, &desc);
        QCOMPARE(name, QStringLiteral("b"));
        QCOMPARE(desc, QStringLiteral("line one line two")); // one paragraph in the prompt
        Skills::parseFrontMatter(QStringLiteral("---\ndescription: starts here\n  and goes on\nname: c\n---\n"), &name, &desc);
        QCOMPARE(desc, QStringLiteral("starts here and goes on"));
        QCOMPARE(name, QStringLiteral("c"));
        Skills::parseFrontMatter(QStringLiteral("---\ndescription: \"say \\\"hi\\\"\"\n---\n"), &name, &desc);
        QCOMPARE(desc, QStringLiteral("say \"hi\""));
        Skills::parseFrontMatter(QStringLiteral("no front matter\nname: x\n"), &name, &desc);
        QCOMPARE(name, QString());
        QCOMPARE(desc, QString());
    }

    void discoverFindsSkillFolders()
    {
        const QList<Skills::Skill> skills = Skills::discover(root());
        QStringList names;
        for (const auto &s : skills)
            names << s.name;
        QCOMPARE(names, (QStringList{QStringLiteral("bare"), QStringLiteral("sm-inventory"), QStringLiteral("weather")}));
        QCOMPARE(skills.at(1).description, QStringLiteral("Query the store's inventory."));
        QCOMPARE(skills.at(2).description, QStringLiteral("Look up the forecast for a city."));
        QCOMPARE(skills.at(0).description, QString());
        QVERIFY(Skills::discover(m_dir.filePath(QStringLiteral("missing"))).isEmpty());
    }

    void discoverMergesRootsEarlierFirst()
    {
        // A second root with its own skill and another copy of "weather".
        const QString other = m_dir.filePath(QStringLiteral("fallback"));
        QDir().mkpath(other + QStringLiteral("/weather"));
        QDir().mkpath(other + QStringLiteral("/alpha"));
        QFile w(other + QStringLiteral("/weather/SKILL.md"));
        QVERIFY(w.open(QIODevice::WriteOnly));
        w.write("---\nname: weather\ndescription: The fallback copy.\n---\n");
        w.close();
        QFile a(other + QStringLiteral("/alpha/SKILL.md"));
        QVERIFY(a.open(QIODevice::WriteOnly));
        a.write("---\nname: alpha\ndescription: Only in the fallback.\n---\n");
        a.close();

        const auto skills = Skills::discover(QStringList{root(), other});
        QStringList names;
        for (const auto &s : skills)
            names << s.name;
        QCOMPARE(names, (QStringList{QStringLiteral("alpha"), QStringLiteral("bare"), QStringLiteral("sm-inventory"),
                                     QStringLiteral("weather")}));
        QCOMPARE(skills.at(0).dir, other + QStringLiteral("/alpha"));
        QCOMPARE(skills.at(3).description, QStringLiteral("Look up the forecast for a city."));
        QCOMPARE(skills.at(3).dir, root() + QStringLiteral("/weather"));

        // In the other order the fallback's copy wins.
        const auto swapped = Skills::discover(QStringList{other, root()});
        QCOMPARE(swapped.at(3).description, QStringLiteral("The fallback copy."));
        QDir(other).removeRecursively();
    }

    void promptListsNamesAndDescriptions()
    {
        const auto skills = Skills::discover(root());
        const QString p = Skills::promptSection(skills, true);
        QVERIFY(p.contains(QLatin1String("load_skill")));
        QVERIFY(p.contains(QLatin1String("- sm-inventory: Query the store's inventory.")));
        QVERIFY(p.contains(QLatin1String("- bare: (no description)")));
        QVERIFY(!p.contains(QLatin1String("Run scripts/stock.sh"))); // instructions are loaded on demand
        QVERIFY(!p.contains(QLatin1String("turned off")));
        QVERIFY(Skills::promptSection(skills, false).contains(QLatin1String("Running commands is turned off")));
        QCOMPARE(Skills::promptSection({}, true), QString());
    }

    void loadGivesInstructionsFolderAndFiles()
    {
        const auto skills = Skills::discover(root());
        QString err;
        const QString t = Skills::load(skills, QStringLiteral("SM-Inventory"), {}, 64 * 1024, &err);
        QCOMPARE(err, QString());
        QVERIFY(t.startsWith(QLatin1String("Skill: sm-inventory\nFolder: ") + QFileInfo(root() + QStringLiteral("/sm-inventory")).absoluteFilePath()));
        QVERIFY(t.contains(QLatin1String("Other files (load one with the file argument): logo.png, references/api.md, scripts/stock.sh")));
        QVERIFY(t.contains(QLatin1String("# Inventory\nRun scripts/stock.sh.")));
        // The folder name works too when the front matter's name was not usable.
        QVERIFY(Skills::load(skills, QStringLiteral("weather"), {}, 64 * 1024, &err).contains(QLatin1String("Use curl.")));
        const QString ref = Skills::load(skills, QStringLiteral("sm-inventory"), QStringLiteral("references/api.md"), 64 * 1024, &err);
        QCOMPARE(err, QString());
        QVERIFY(ref.contains(QLatin1String("File: references/api.md\n\nAPI notes")));
    }

    void loadRefusesWhatItShouldNot()
    {
        const auto skills = Skills::discover(root());
        QString err;
        QCOMPARE(Skills::load(skills, QStringLiteral("nope"), {}, 65536, &err), QString());
        QCOMPARE(err, QStringLiteral("No skill named \"nope\". Installed: bare, sm-inventory, weather."));
        for (const QString &bad : {QStringLiteral("../secret.txt"), QStringLiteral("../../secret.txt"),
                                   m_dir.filePath(QStringLiteral("skills/secret.txt")), QStringLiteral("missing.md")})
        {
            err.clear();
            QCOMPARE(Skills::load(skills, QStringLiteral("sm-inventory"), bad, 65536, &err), QString());
            QVERIFY2(err.startsWith(QLatin1String("No file")), qPrintable(bad + QLatin1String(": ") + err));
        }
        err.clear();
        QCOMPARE(Skills::load(skills, QStringLiteral("sm-inventory"), QStringLiteral("scripts"), 65536, &err), QString());
        QVERIFY(err.contains(QLatin1String("is not a file")));
        err.clear();
        QCOMPARE(Skills::load(skills, QStringLiteral("sm-inventory"), QStringLiteral("logo.png"), 65536, &err), QString());
        QVERIFY(err.contains(QLatin1String("is not a text file")));
#ifdef Q_OS_UNIX
        // A link inside the folder that leads out of it is refused as well.
        QVERIFY(QFile::link(root() + QStringLiteral("/secret.txt"), root() + QStringLiteral("/sm-inventory/link.txt")));
        err.clear();
        QCOMPARE(Skills::load(skills, QStringLiteral("sm-inventory"), QStringLiteral("link.txt"), 65536, &err), QString());
        QVERIFY(err.startsWith(QLatin1String("No file")));
        QFile::remove(root() + QStringLiteral("/sm-inventory/link.txt"));
#endif
        QCOMPARE(Skills::load({}, QStringLiteral("x"), {}, 65536, &err), QString());
        QCOMPARE(err, QStringLiteral("There are no skills installed."));
    }

    void loadIsCutToFit()
    {
        QVERIFY(write(QStringLiteral("big/SKILL.md"), "---\nname: big\ndescription: d\n---\n" + QByteArray(50000, 'x')));
        const auto skills = Skills::discover(root());
        QString err;
        const QString t = Skills::load(skills, QStringLiteral("big"), {}, 4000, &err);
        QVERIFY(t.endsWith(QLatin1String("(Cut to fit the model's context window.)")));
        QVERIFY(t.toUtf8().size() < 4100);
        QVERIFY(QDir(root() + QStringLiteral("/big")).removeRecursively());
    }

    void toolDefinitionShape()
    {
        const QJsonObject fn = Skills::toolDefinition().value(QStringLiteral("function")).toObject();
        QCOMPARE(fn.value(QStringLiteral("name")).toString(), QStringLiteral("load_skill"));
        QVERIFY(fn.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("properties")).toObject().contains(QStringLiteral("file")));
    }
};

QTEST_GUILESS_MAIN(TestSkills)
#include "test_skills.moc"
