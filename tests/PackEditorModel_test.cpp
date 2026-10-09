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
    void targetStatesDriveStatusSummary();
    void packEditorVisibilityDoesNotRequireAuthorConfig();
    void schemaOneAndTwoImportsRemainSupported();
    void targetMutationUsesFrozenBridgeDto();
    void unifiedInventoryAndCombinedFilters();
    void identitySurvivesSortAndMetadataRefresh();
    void missingMetadataAndEmptyInventory();
    void targetAssignmentRequiresNonEmptyLogicalTargets();
    void workspaceVisibilityKeepsPromotionRoute();
    void duplicateTargetObservationsStayOneLogicalResource();
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

void PackEditorModelTest::targetStatesDriveStatusSummary()
{
    const QJsonObject resource{
        {"status", "conflict"},
        {"target_states", QJsonArray{QJsonObject{{"target_id", "client"}, {"status", "synced"}},
                                     QJsonObject{{"target_id", "server"}, {"status", "modified"}, {"message", "local edit"}}}},
    };
    QCOMPARE(packEditorStatusSummary(resource), QStringLiteral("client: synced; server: modified"));
    QCOMPARE(packEditorStatusSummary(QJsonObject{{"status", "missing"}}), QStringLiteral("missing"));
}

void PackEditorModelTest::packEditorVisibilityDoesNotRequireAuthorConfig()
{
    QVERIFY(packEditorPageShouldDisplay(true));
    QVERIFY(!packEditorPageShouldDisplay(false));
}

void PackEditorModelTest::schemaOneAndTwoImportsRemainSupported()
{
    QVERIFY(modLockImportSchemaSupported(1));
    QVERIFY(modLockImportSchemaSupported(2));
    QVERIFY(modLockImportSchemaSupported(3));
    QVERIFY(!modLockImportSchemaSupported(0));
    QVERIFY(!modLockImportSchemaSupported(4));
}

void PackEditorModelTest::targetMutationUsesFrozenBridgeDto()
{
    const auto params = packEditorSetModTargetsParams(QStringLiteral("modrinth:create"), QJsonArray{"server"});
    QCOMPARE(params.keys(), QStringList({QStringLiteral("id"), QStringLiteral("targets")}));
    QCOMPARE(params.value("id").toString(), QStringLiteral("modrinth:create"));
    QCOMPARE(params.value("targets").toArray(), QJsonArray{"server"});
}

void PackEditorModelTest::unifiedInventoryAndCombinedFilters()
{
    const QJsonArray entries{
        QJsonObject{{"id", "modrinth:sodium"}, {"identity", "modrinth:sodium"}, {"name", "Sodium"}, {"provider", "Modrinth"}, {"filename", "sodium.jar"}, {"targets", QJsonArray{"client"}}, {"status", "synced"}},
        QJsonObject{{"id", "curseforge:chunky"}, {"identity", "curseforge:chunky"}, {"name", "Chunky"}, {"provider", "CurseForge"}, {"filename", "chunky.jar"}, {"targets", QJsonArray{"server"}}, {"status", "synced"}},
        QJsonObject{{"id", "modrinth:create"}, {"identity", "modrinth:create"}, {"name", "Create"}, {"provider", "Modrinth"}, {"filename", "create.jar"}, {"targets", QJsonArray{"client", "server"}}, {"status", "modified"}},
    };
    PackEditorInventoryModel source;
    PackEditorInventoryFilter proxy;
    proxy.setSourceModel(&source);
    source.setEntries(entries);
    QCOMPARE(source.rowCount(), 3); // shared Create is represented once.
    proxy.setTargetFilter(QStringLiteral("client"));
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setTargetFilter(QStringLiteral("server"));
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setTargetFilter(QStringLiteral("shared"));
    QCOMPARE(proxy.rowCount(), 1);
    proxy.setTargetFilter(QStringLiteral("all"));
    proxy.setStateFilter(QStringLiteral("changed"));
    proxy.setSearchText(QStringLiteral("create.jar"));
    QCOMPARE(proxy.rowCount(), 1);
    proxy.setTargetFilter(QStringLiteral("client"));
    QCOMPARE(proxy.rowCount(), 1);
    proxy.setSearchText(QStringLiteral("curseforge"));
    QCOMPARE(proxy.rowCount(), 0);
    source.setEntries(QJsonArray{QJsonObject{{"id", "legacy"}, {"name", "Old format"}, {"status", "synced"}}});
    proxy.setSearchText(QString());
    proxy.setStateFilter(QStringLiteral("all"));
    proxy.setTargetFilter(QStringLiteral("client"));
    QCOMPARE(proxy.rowCount(), 1); // Legacy resources keep the historical client default.
}

void PackEditorModelTest::identitySurvivesSortAndMetadataRefresh()
{
    PackEditorInventoryModel source;
    PackEditorInventoryFilter proxy;
    proxy.setSourceModel(&source);
    source.setEntries(QJsonArray{
        QJsonObject{{"identity", "b"}, {"name", "Zulu"}, {"targets", QJsonArray{"client"}}, {"status", "synced"}},
        QJsonObject{{"identity", "a"}, {"name", "Alpha"}, {"targets", QJsonArray{"server"}}, {"status", "synced"}},
    });
    proxy.sort(PackEditorInventoryModel::NameColumn, Qt::AscendingOrder);
    QCOMPARE(proxy.index(0, 0).data(PackEditorInventoryModel::IdentityRole).toString(), QStringLiteral("a"));
    source.setEntries(QJsonArray{
        QJsonObject{{"identity", "b"}, {"name", "Beta"}, {"targets", QJsonArray{"client"}}, {"status", "synced"}},
        QJsonObject{{"identity", "a"}, {"name", "Alpha"}, {"targets", QJsonArray{"server"}}, {"status", "synced"}},
    });
    QVERIFY(source.rowForIdentity(QStringLiteral("a")) >= 0);
    QCOMPARE(proxy.index(0, 0).data(PackEditorInventoryModel::IdentityRole).toString(), QStringLiteral("a"));
}

void PackEditorModelTest::missingMetadataAndEmptyInventory()
{
    PackEditorInventoryModel model;
    model.setEntries({});
    QCOMPARE(model.rowCount(), 0);
    model.setEntries(QJsonArray{QJsonObject{{"id", "legacy:unknown"}, {"filename", "unknown.jar"}, {"status", "unmanaged"}}});
    QCOMPARE(model.index(0, PackEditorInventoryModel::NameColumn).data().toString(), QStringLiteral("unknown.jar"));
    QCOMPARE(model.index(0, PackEditorInventoryModel::ProviderColumn).data().toString(), QStringLiteral("Unknown source"));
}

void PackEditorModelTest::targetAssignmentRequiresNonEmptyLogicalTargets()
{
    QVERIFY(packEditorTargetsAreValid(QJsonArray{"client"}));
    QVERIFY(packEditorTargetsAreValid(QJsonArray{"server"}));
    QVERIFY(packEditorTargetsAreValid(QJsonArray{"client", "server"}));
    QVERIFY(!packEditorTargetsAreValid({}));
    QVERIFY(!packEditorTargetsAreValid(QJsonArray{"both"}));
    const auto params = packEditorSetModTargetsParams(QStringLiteral("modrinth:create"), QJsonArray{"client", "server"});
    QCOMPARE(params.value("targets").toArray(), QJsonArray({QStringLiteral("client"), QStringLiteral("server")}));
}

void PackEditorModelTest::workspaceVisibilityKeepsPromotionRoute()
{
    QVERIFY(packEditorPageShouldDisplay(true, true, false));
    QVERIFY(packEditorPageShouldDisplay(true, false, true));
    QVERIFY(!packEditorPageShouldDisplay(true, false, false));
    QVERIFY(!packEditorPageShouldDisplay(false, true, true));
}

void PackEditorModelTest::duplicateTargetObservationsStayOneLogicalResource()
{
    PackEditorInventoryModel model;
    model.setEntries(QJsonArray{
        QJsonObject{{"id", "modrinth:sample"}, {"identity", "modrinth:sample"}, {"filename", "sample.jar"}, {"name", "Sample"},
                    {"managed", true}, {"targets", QJsonArray{"client"}}, {"status", "synced"},
                    {"target_states", QJsonArray{QJsonObject{{"target_id", "client"}, {"status", "synced"}}}}},
        QJsonObject{{"id", "modrinth:sample"}, {"identity", "modrinth:sample"}, {"filename", "sample.jar"}, {"name", "sample.jar"},
                    {"managed", false}, {"targets", QJsonArray{}}, {"status", "unmanaged"},
                    {"target_states", QJsonArray{QJsonObject{{"target_id", "server"}, {"status", "unmanaged"}}}}},
    });
    QCOMPARE(model.rowCount(), 1);
    const auto item = model.entryAt(0);
    QVERIFY(item.value("managed").toBool());
    QCOMPARE(item.value("targets").toArray(), QJsonArray{"client"});
    QCOMPARE(item.value("target_states").toArray().size(), 2);
}

QTEST_GUILESS_MAIN(PackEditorModelTest)
#include "PackEditorModel_test.moc"
