// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "modlock/ModLockBridge.h"

class ModLockBridgeTest : public QObject {
    Q_OBJECT

   private slots:
    void compatibilityHandshake();
    void rejectsIncompatiblePayload();
    void parsesFragmentedProgressAndResult();
    void cancellationIsCooperative();
    void preservesStructuredErrors();
    void diagnosticsStayOnStderrChannel();
};

static QTemporaryDir makeBridgeRoot()
{
    return QTemporaryDir(QDir::tempPath() + QStringLiteral("/ModLock bridge данные with spaces-XXXXXX"));
}

void ModLockBridgeTest::compatibilityHandshake()
{
    auto root = makeBridgeRoot();
    QVERIFY(root.isValid());
    ModLockBridge bridge(root.path());
    QSignalSpy completed(&bridge, &ModLockBridge::completed);
    QSignalSpy failed(&bridge, &ModLockBridge::failed);
    QSignalSpy finished(&bridge, &ModLockBridge::finished);

    QVERIFY(bridge.startCompatibilityCheck());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.front().at(1).toJsonObject().value("loader_protocol").toInt(), ModLockBridge::Protocol);
}

void ModLockBridgeTest::rejectsIncompatiblePayload()
{
    auto root = makeBridgeRoot();
    QVERIFY(root.isValid());
    ModLockBridge bridge(root.path());
    QSignalSpy failed(&bridge, &ModLockBridge::failed);
    QSignalSpy finished(&bridge, &ModLockBridge::finished);

    QVERIFY(bridge.start("capabilities", {{"loader_protocol", 2}}));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.front().at(1).toJsonObject().value("code").toString(), QStringLiteral("unsupported_protocol"));
}

void ModLockBridgeTest::parsesFragmentedProgressAndResult()
{
    auto root = makeBridgeRoot();
    QVERIFY(root.isValid());
    ModLockBridge bridge(root.path());
    QSignalSpy progress(&bridge, &ModLockBridge::progress);
    QSignalSpy completed(&bridge, &ModLockBridge::completed);
    QSignalSpy finished(&bridge, &ModLockBridge::finished);

    QVERIFY(bridge.start("scan"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.front().at(1).toString(), QStringLiteral("scan started"));
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.front().at(1).toJsonObject().value("ok").toBool(), true);
}

void ModLockBridgeTest::cancellationIsCooperative()
{
    auto root = makeBridgeRoot();
    QVERIFY(root.isValid());
    ModLockBridge bridge(root.path());
    QSignalSpy failed(&bridge, &ModLockBridge::failed);
    QSignalSpy finished(&bridge, &ModLockBridge::finished);
    connect(&bridge, &ModLockBridge::progress, &bridge, [&bridge](const QString&, const QString&) { QVERIFY(bridge.cancel()); });

    QVERIFY(bridge.start("cancel-test"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.front().at(1).toJsonObject().value("code").toString(), QStringLiteral("cancelled"));
}

void ModLockBridgeTest::preservesStructuredErrors()
{
    auto root = makeBridgeRoot();
    QVERIFY(root.isValid());
    ModLockBridge bridge(root.path());
    QSignalSpy failed(&bridge, &ModLockBridge::failed);
    QSignalSpy finished(&bridge, &ModLockBridge::finished);

    QVERIFY(bridge.start("structured-error"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(failed.count(), 1);
    const QJsonObject error = failed.front().at(1).toJsonObject();
    QCOMPARE(error.value("code").toString(), QStringLiteral("network"));
    QCOMPARE(error.value("message").toString(), QStringLiteral("offline"));
}

void ModLockBridgeTest::diagnosticsStayOnStderrChannel()
{
    auto root = makeBridgeRoot();
    QVERIFY(root.isValid());
    ModLockBridge bridge(root.path());
    QSignalSpy diagnostic(&bridge, &ModLockBridge::diagnostic);
    QSignalSpy finished(&bridge, &ModLockBridge::finished);

    QVERIFY(bridge.start("scan"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(diagnostic.count(), 1);
    QCOMPARE(diagnostic.front().front().toString(), QStringLiteral("fake diagnostic"));
}

QTEST_GUILESS_MAIN(ModLockBridgeTest)
#include "ModLockBridge_test.moc"
