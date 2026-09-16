#include "openai/ToolCallXml.h"

#include <QtTest>

class TestToolCallXml : public QObject
{
    Q_OBJECT
private slots:
    void ignoresPlainText()
    {
        QVERIFY(!ToolCallXml::looksLike(QStringLiteral("Here is the report.")));
        QVERIFY(ToolCallXml::parse(QStringLiteral("Here is the report.")).isEmpty());
    }

    void parsesBareXmlDump()
    {
        const QString s = QStringLiteral(
            "Here's your interactive HTML report with all today's sales data:\n"
            "<tool_call> <function=clover_metrics> <parameter=groupby> hour_of_day");
        QVERIFY(ToolCallXml::looksLike(s));
        const auto calls = ToolCallXml::parse(s);
        QCOMPARE(calls.size(), 1);
        const QJsonObject fn = calls.at(0).toObject().value(QStringLiteral("function")).toObject();
        QCOMPARE(fn.value(QStringLiteral("name")).toString(), QString("clover_metrics"));
        QVERIFY(fn.value(QStringLiteral("arguments")).toString().contains(QLatin1String("groupby")));
        QVERIFY(fn.value(QStringLiteral("arguments")).toString().contains(QLatin1String("hour_of_day")));
    }

    void parsesNamedFunctionAndParams()
    {
        const QString s = QStringLiteral(
            "<tool_call><function name=\"clover_metrics\">"
            "<parameter name=\"query\">sales</parameter>"
            "<parameter name=\"range\">today</parameter>"
            "</function></tool_call>");
        const auto calls = ToolCallXml::parse(s);
        QCOMPARE(calls.size(), 1);
        const QJsonObject fn = calls.at(0).toObject().value(QStringLiteral("function")).toObject();
        QCOMPARE(fn.value(QStringLiteral("name")).toString(), QString("clover_metrics"));
        const QJsonObject args = QJsonDocument::fromJson(
                                     fn.value(QStringLiteral("arguments")).toString().toUtf8())
                                     .object();
        QCOMPARE(args.value(QStringLiteral("query")).toString(), QString("sales"));
        QCOMPARE(args.value(QStringLiteral("range")).toString(), QString("today"));
    }

    void parsesMultilineWebFetch()
    {
        const QString s = QStringLiteral(
            "Here's the complete interactive dashboard with all the sales data:\n"
            "\n"
            "<tool_call>\n"
            "<function=web_fetch>\n"
            "<parameter=url>\n"
            "https://cdn.jsdelivr.net/npm/chart.js@4.4.7/dist/chart.umd.min.js\n"
            "</parameter>\n"
            "</function>\n"
            "</tool_call>\n");
        const auto calls = ToolCallXml::parse(s);
        QCOMPARE(calls.size(), 1);
        const QJsonObject fn = calls.at(0).toObject().value(QStringLiteral("function")).toObject();
        QCOMPARE(fn.value(QStringLiteral("name")).toString(), QString("web_fetch"));
        QVERIFY(fn.value(QStringLiteral("arguments")).toString().contains(QLatin1String("chart.umd.min.js")));
        const QString stripped = ToolCallXml::strip(s);
        QVERIFY(!stripped.contains(QLatin1String("<tool_call")));
        QVERIFY(stripped.contains(QLatin1String("complete interactive dashboard")));
    }

    void stripLeavesPreamble()
    {
        const QString s = QStringLiteral(
            "Now here's the interactive dashboard:\n"
            "<tool_call> <function=web_fetch> <parameter=url> "
            "https://cdn.jsdelivr.net/npm/chart.js@4.4.7/dist/chart.umd.min.js\n"
            "</parameter></function></tool_call>");
        QCOMPARE(ToolCallXml::strip(s), QString("Now here's the interactive dashboard:"));
    }
};

QTEST_MAIN(TestToolCallXml)
#include "test_toolcall_xml.moc"
