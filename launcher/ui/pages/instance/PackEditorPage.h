// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QIcon>
#include <QWidget>
#include <QVector>

#include "tasks/Task.h"

#include "modlock/PackEditorModel.h"
#include "modplatform/ModIndex.h"
#include "ui/pages/BasePage.h"

class BaseInstance;
class MinecraftInstance;
class ModFolderModel;
class ModLockBridge;
class QCheckBox;
class QComboBox;
class QLabel;
class QListView;
class QPushButton;
class QTableWidget;
class QTableView;
class QToolButton;
class QLineEdit;
class QNetworkReply;
class PackEditorTableModel;
class QResizeEvent;
class QSplitter;
class QGridLayout;

class PackEditorPage final : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit PackEditorPage(BaseInstance* instance, QWidget* parent = nullptr);
    ~PackEditorPage() override = default;

    QString id() const override { return "pack_editor"; }
    QString displayName() const override { return tr("Pack Editor"); }
    QIcon icon() const override;
    QString helpPage() const override { return "ModLock-Pack-Editor"; }
    bool shouldDisplay() const override;
    bool prepareToClose() override;
    void openedImpl() override;

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private:
    void loadAuthorState();
    void refreshView();
    void requestVisibleIcons();
    void requestModIcon(const QJsonObject& mod);
    void resolveProviderMetadata();
    bool applyLauncherMetadata();
    void setBusy(bool busy);
    void runOperation(const QString& operation,
                      const QJsonObject& params,
                      std::function<void(const QJsonObject&)> onSuccess = {});
    void onOperationFinished();
    QJsonObject selectedMod() const;
    void showSelectedMod(const QJsonObject& mod);
    void setSelectedTargets(const QJsonArray& targets);
    void setSelectedResourceState(const QString& state);
    void setSelectedFileState(const QString& state);
    void removeSelectedFile();
    void addMods(const QJsonObject& existingMod = {});
    void addSelectedMod(ModPlatform::IndexedVersion version, ModPlatform::IndexedPack::Ptr pack, const QStringList& targets);
    void addTrackedPath();
    void removeTrackedPath();
    void removeSelectedResource();
    void showPublishPreview();
    void publish(const QString& commitMessage);
    void showStatusMessage(const QString& message, bool error = false);

    BaseInstance* m_instance = nullptr;
    MinecraftInstance* m_minecraftInstance = nullptr;
    ModFolderModel* m_modsModel = nullptr;
    PackEditorAuthorState m_state;
    QJsonObject m_retainedSelection;
    PackEditorInventoryModel* m_inventoryModel = nullptr;
    PackEditorInventoryFilter* m_inventoryFilter = nullptr;
    QTableView* m_inventoryView = nullptr;
    QSplitter* m_inventorySplitter = nullptr;
    QGridLayout* m_inventoryToolbar = nullptr;
    QGridLayout* m_headerLayout = nullptr;
    QLabel* m_pageTitle = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_targetFilter = nullptr;
    QComboBox* m_stateFilter = nullptr;
    QCheckBox* m_targetClient = nullptr;
    QCheckBox* m_targetServer = nullptr;
    QPushButton* m_applyTargetsButton = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_inventorySummary = nullptr;
    QLabel* m_selectedDetails = nullptr;
    QToolButton* m_technicalToggle = nullptr;
    QLabel* m_technicalDetails = nullptr;
    QTableView* m_filesTable = nullptr;
    QTableView* m_ignoredTable = nullptr;
    QTableView* m_trackedTable = nullptr;
    PackEditorTableModel* m_filesModel = nullptr;
    PackEditorTableModel* m_ignoredModel = nullptr;
    PackEditorTableModel* m_trackedModel = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_promoteLockButton = nullptr;
    QPushButton* m_scanButton = nullptr;
    QPushButton* m_previewButton = nullptr;
    QPushButton* m_addModButton = nullptr;
    QPushButton* m_updateModButton = nullptr;
    QPushButton* m_removeResourceButton = nullptr;
    QPushButton* m_addTrackedButton = nullptr;
    QPushButton* m_removeTrackedButton = nullptr;
    QPushButton* m_ignoreFileButton = nullptr;
    QPushButton* m_unmanageFileButton = nullptr;
    QPushButton* m_removeFileButton = nullptr;
    QPushButton* m_ignoreButton = nullptr;
    QPushButton* m_unmanageButton = nullptr;
    QPointer<ModLockBridge> m_bridge;
    QHash<QString, QIcon> m_iconCache;
    QSet<QString> m_iconRequests;
    QSet<QString> m_metadataRequested;
    QVector<Task::Ptr> m_metadataTasks;
    QSet<QString> m_launcherCachedIconIdentities;
    bool m_launcherMetadataLoadRequested = false;
    bool m_hasWorkspace = false;
    bool m_compactLayout = false;
    QString m_operation;
    QString m_previewId;
    QJsonObject m_result;
    QJsonObject m_error;
    std::function<void(const QJsonObject&)> m_onSuccess;
};
