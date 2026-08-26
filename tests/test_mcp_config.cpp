#include "mcp/McpConfig.h"

#include <QJsonDocument>
#include <QtTest>

static const char kClaudeJson[] =
    "{\"mcpServers\":{"
    "\"pyclover\":{\"command\":\"/venv/bin/python\",\"args\":[\"/tmp/pyclover_mcp.py\"]},"
    "\"rapidid\":{\"command\":\"/opt/mcp-rapidid\",\"env\":{\"RI_HOST\":\"https://example.test\"}}"
    "},\"preferences\":{\"sidebarMode\":\"chat\"}}";

static const char kDisabledJson[] =
    "{\"mcpServers\":{\"x\":{\"command\":\"echo\",\"disabled\":true}}}";

static const char kPrefsJson[] =
    "{\"mcpServers\":{\"a\":{\"command\":\"x\"}},\"preferences\":{}}";

class TestMcpConfig : public QObject
{
    Q_OBJECT
private slots:
    void defaultPathIsPlatformConfig()
    {
        const QString p = McpConfig::defaultPath();
        QVERIFY(p.endsWith(QStringLiteral("config.json")));
#ifdef Q_OS_LINUX
        QVERIFY(p.contains(QStringLiteral("/.shammy/")));
#endif
    }

    void parseClaudeShape()
    {
        const QJsonObject root = QJsonDocument::fromJson(kClaudeJson).object();
        const auto servers = McpConfig::parseServers(root);
        QCOMPARE(servers.size(), 2);
        QCOMPARE(servers.at(0).name == QLatin1String("pyclover") ? servers.at(0).args.size()
                                                                 : servers.at(1).args.size(),
                 1);
        bool foundEnv = false;
        for (const auto &s : servers)
        {
            if (s.name == QLatin1String("rapidid"))
            {
                QVERIFY(s.args.isEmpty());
                QCOMPARE(s.env.value("RI_HOST"), QString("https://example.test"));
                foundEnv = true;
            }
        }
        QVERIFY(foundEnv);
    }

    void roundTripOmitsEmptyArgsAndKeepsExtras()
    {
        McpServerConfig c;
        c.name = QStringLiteral("rapidid");
        c.command = QStringLiteral("/opt/mcp-rapidid");
        c.env.insert(QStringLiteral("RI_HOST"), QStringLiteral("https://example.test"));
        c.extra.insert(QStringLiteral("type"), QStringLiteral("stdio"));
        QJsonObject root;
        root.insert(QStringLiteral("preferences"), QJsonObject{{QStringLiteral("x"), 1}});
        const QJsonObject out = McpConfig::mergeServers(root, {c});
        QVERIFY(out.contains("preferences"));
        const QJsonObject s = out.value("mcpServers").toObject().value("rapidid").toObject();
        QCOMPARE(s.value("command").toString(), QString("/opt/mcp-rapidid"));
        QVERIFY(!s.contains("args"));
        QCOMPARE(s.value("env").toObject().value("RI_HOST").toString(),
                 QString("https://example.test"));
        QCOMPARE(s.value("type").toString(), QString("stdio"));
        QVERIFY(!s.contains("disabled"));
    }

    void disabledFlag()
    {
        const QJsonObject root = QJsonDocument::fromJson(kDisabledJson).object();
        const auto servers = McpConfig::parseServers(root);
        QCOMPARE(servers.size(), 1);
        QVERIFY(!servers.at(0).enabled);
        const QJsonObject out = McpConfig::mergeServers({}, servers);
        QVERIFY(out.value("mcpServers").toObject().value("x").toObject().value("disabled").toBool());
    }

    void mcpServersOnlyDropsClaudePrefs()
    {
        const QJsonObject root = QJsonDocument::fromJson(kPrefsJson).object();
        const QJsonObject slim = McpConfig::mcpServersOnly(root);
        QVERIFY(slim.contains("mcpServers"));
        QVERIFY(!slim.contains("preferences"));
    }
};

QTEST_MAIN(TestMcpConfig)
#include "test_mcp_config.moc"
