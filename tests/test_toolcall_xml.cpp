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
};

QTEST_MAIN(TestToolCallXml)
#include "test_toolcall_xml.moc"
