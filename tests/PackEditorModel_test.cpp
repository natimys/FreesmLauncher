// SPDX-License-Identifier: GPL-3.0-only
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QTest>

#include "modlock/ModLockConflictDialog.h"
#include "modlock/ModLockBridge.h"
#include "modlock/PackEditorModel.h"

class PackEditorModelTest : public QObject {
    Q_OBJECT

   private slots:
    void groupsSchemaThreeTargets();
    void parsesSchemaThreeAuthorState();
    void hidesSharedEntriesWithoutChangingState();
    void keepsLegacyEntriesClientOnly();
    void conflictKindsDriveConfirmation();
    void targetRootsAreDerivedFromLauncherPaths();
};

void PackEditorModelTest::groupsSchemaThreeTargets()
{
    const QJsonArray entries{
        QJsonObject{{"id", "modrinth:sodium"}, {"name", "Sodium"}, {"targets", QJsonArray{"client"}}},
        QJsonObject{{"id", "modrinth:chunky"}, {"name", "Chunky"}, {"targets", QJsonArray{"server"}}},
        QJsonObject{{"id", "modrinth:create"}, {"name", "Create"}, {"targets", QJsonArray{"client", "server"}}},
    };
    PackEditorTargetModel client("client");
    PackEditorTargetModel server("server");
    client.setEntries(entries);
    server.setEntries(entries);
    QCOMPARE(client.rowCount(), 2);
    QCOMPARE(server.rowCount(), 2);
    QCOMPARE(client.data(client.index(0), Qt::DisplayRole).toString(), QStringLiteral("Sodium"));
    QCOMPARE(server.data(server.index(0), Qt::DisplayRole).toString(), QStringLiteral("Chunky"));
    QCOMPARE(client.data(client.index(1), PackEditorTargetModel::IdentityRole).toString(), QStringLiteral("modrinth:create"));
    QVERIFY(client.data(client.index(1), PackEditorTargetModel::SharedRole).toBool());
}

void PackEditorModelTest::hidesSharedEntriesWithoutChangingState()
{
    const QJsonArray entries{QJsonObject{{"id", "shared"}, {"targets", QJsonArray{"client", "server"}}}};
    PackEditorTargetModel client("client");
    client.setEntries(entries);
    QCOMPARE(client.rowCount(), 1);
    client.setShowShared(false);
    QCOMPARE(client.rowCount(), 0);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().toObject().value("targets").toArray().size(), 2);
}

void PackEditorModelTest::parsesSchemaThreeAuthorState()
{
    const QJsonObject bridgeResult{
        {"lock", QJsonObject{{"schema", 3}}},
        {"settings", QJsonObject{{"include_dirs", QJsonArray{"kubejs"}}}},
        {"mods", QJsonArray{QJsonObject{{"id", "modrinth:create"}, {"targets", QJsonArray{"client", "server"}}}}},
        {"files", QJsonArray{QJsonObject{{"path", "files/kubejs/foo.js"}, {"targets", QJsonArray{"server"}}}}},
        {"tracked_paths", QJsonArray{QJsonObject{{"path", "kubejs"}}}},
        {"branch", "main"},
        {"dirty", true},
    };
    PackEditorAuthorState state;
    QString error;
    QVERIFY2(PackEditorAuthorState::parse(bridgeResult, &state, &error), qPrintable(error));
    QCOMPARE(state.lock.toObject().value("schema").toInt(), 3);
    QCOMPARE(state.mods.size(), 1);
    QCOMPARE(state.mods.first().toObject().value("targets").toArray().size(), 2);
    QCOMPARE(state.files.size(), 1);
    QCOMPARE(state.trackedPaths.size(), 1);
    QCOMPARE(state.branch, QStringLiteral("main"));
    QVERIFY(state.dirty);
}

void PackEditorModelTest::keepsLegacyEntriesClientOnly()
{
    const QJsonArray entries{QJsonObject{{"id", "legacy"}}};
    PackEditorTargetModel client("client");
    PackEditorTargetModel server("server");
    client.setEntries(entries);
    server.setEntries(entries);
    QCOMPARE(client.rowCount(), 1);
    QCOMPARE(server.rowCount(), 0);
}

void PackEditorModelTest::conflictKindsDriveConfirmation()
{
    const QString hash(64, QLatin1Char('a'));
    QVERIFY(ModLockConflictDialog::isConfirmable({{"kind", "locally_modified"}, {"sha256", hash}}));
    QVERIFY(ModLockConflictDialog::isConfirmable({{"kind", "existing_unmanaged"}, {"sha256", hash}}));
    QVERIFY(!ModLockConflictDialog::isConfirmable({{"kind", "target_not_regular"}, {"sha256", hash}}));
    QVERIFY(!ModLockConflictDialog::isConfirmable({{"kind", "changed_during_apply"}, {"sha256", hash}}));
}

void PackEditorModelTest::targetRootsAreDerivedFromLauncherPaths()
{
    const auto roots = modLockTargetRoots(QStringLiteral("C:/instances/pack/minecraft"), QStringLiteral("C:/instances/pack"));
    QCOMPARE(QDir::cleanPath(roots.value("client").toString()), QStringLiteral("C:/instances/pack/minecraft"));
    QCOMPARE(QDir::cleanPath(roots.value("server").toString()), QStringLiteral("C:/instances/pack/server"));
}

QTEST_GUILESS_MAIN(PackEditorModelTest)
#include "PackEditorModel_test.moc"
