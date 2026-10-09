// SPDX-License-Identifier: GPL-3.0-only
#include "PackEditorPage.h"

#include <QCheckBox>
#include <QAbstractTableModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMenu>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QTabWidget>
#include <QSplitter>
#include <QTableWidget>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShortcut>
#include <QTimer>
#include <QUrl>

#include "BaseInstance.h"
#include "Application.h"
#include "modlock/ModLockAddModTask.h"
#include "modlock/ModLockBridge.h"
#include "modlock/PackEditorReviewDialog.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/ModIndex.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlameModIndex.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/ResourceDownloadDialog.h"
#include "ui/pages/modplatform/ResourcePage.h"

namespace {
QStringList targetsFor(const QJsonObject& entry)
{
    QStringList targets;
    for (const auto& value : entry.value("targets").toArray())
        targets.append(value.toString());
    return targets;
}

QString displayTargets(const QJsonObject& entry)
{
    if (entry.value("excluded").toBool()) return QObject::tr("Excluded");
    if (!entry.value("targets").isArray()) return QObject::tr("Client (legacy default)");
    const auto targets = targetsFor(entry);
    if (targets.isEmpty()) return QObject::tr("On this computer only");
    QStringList labels;
    if (targets.contains(QStringLiteral("client"))) labels.append(QObject::tr("Client"));
    if (targets.contains(QStringLiteral("server"))) labels.append(QObject::tr("Server"));
    return labels.join(QStringLiteral(" · "));
}

QJsonArray toTargets(const QStringList& targets)
{
    QJsonArray values;
    for (const auto& target : targets)
        values.append(target);
    return values;
}

QStringList targetsForProviderSide(ModPlatform::Side side)
{
    switch (side) {
        case ModPlatform::Side::ClientSide: return {QStringLiteral("client")};
        case ModPlatform::Side::ServerSide: return {QStringLiteral("server")};
        case ModPlatform::Side::UniversalSide: return {QStringLiteral("client"), QStringLiteral("server")};
        default: return {QStringLiteral("client")};
    }
}

class TargetChoiceDialog final : public QDialog {
   public:
    explicit TargetChoiceDialog(QWidget* parent, const QStringList& initialTargets = {QStringLiteral("client")}) : QDialog(parent)
    {
        setWindowTitle(tr("Install to targets"));
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(tr("Choose where this mod belongs in the pack."), this));
        m_client = new QCheckBox(tr("Client"), this);
        m_server = new QCheckBox(tr("Server"), this);
        m_client->setChecked(initialTargets.contains(QStringLiteral("client")));
        m_server->setChecked(initialTargets.contains(QStringLiteral("server")));
        layout->addWidget(m_client);
        layout->addWidget(m_server);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        auto* ok = buttons->button(QDialogButtonBox::Ok);
        const auto updateEnabled = [this, ok] { ok->setEnabled(m_client->isChecked() || m_server->isChecked()); };
        connect(m_client, &QCheckBox::toggled, this, updateEnabled);
        connect(m_server, &QCheckBox::toggled, this, updateEnabled);
        updateEnabled();
    }

    QStringList targets() const
    {
        QStringList result;
        if (m_client->isChecked()) result.append(QStringLiteral("client"));
        if (m_server->isChecked()) result.append(QStringLiteral("server"));
        return result;
    }

   private:
    QCheckBox* m_client = nullptr;
    QCheckBox* m_server = nullptr;
};
}  // namespace

class PackEditorTableModel final : public QAbstractTableModel {
   public:
    enum class Kind { Files, Tracked, Ignored };
    PackEditorTableModel(Kind kind, QStringList headers, QObject* parent = nullptr)
        : QAbstractTableModel(parent), m_kind(kind), m_headers(std::move(headers)) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_entries.size(); }
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_headers.size(); }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        return orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < m_headers.size()
                   ? QVariant(m_headers.at(section)) : QVariant();
    }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole || index.row() < 0 || index.row() >= m_entries.size())
            return {};
        const auto entry = m_entries.at(index.row()).toObject();
        switch (m_kind) {
            case Kind::Files:
                switch (index.column()) {
                    case 0: return entry.value("target").toString(entry.value("path").toString());
                    case 1: return entry.value("path").toString();
                    case 2: return displayTargets(entry);
                    case 3:
                        if (entry.value("policy").toString() == QStringLiteral("if_missing")) return tr("Install when missing");
                        if (entry.value("policy").toString() == QStringLiteral("replace")) return tr("Update from pack");
                        return entry.value("policy").toString();
                    case 4: return packEditorStatusSummary(entry);
                }
                break;
            case Kind::Tracked:
                switch (index.column()) {
                    case 0: return entry.value("path").toString();
                    case 1: return displayTargets(entry);
                    case 2:
                        if (entry.value("policy").toString() == QStringLiteral("if_missing")) return tr("Install when missing");
                        if (entry.value("policy").toString() == QStringLiteral("replace")) return tr("Update from pack");
                        return entry.value("policy").toString();
                    case 3: return entry.value("enabled").toBool() ? tr("Tracked") : tr("Disabled");
                }
                break;
            case Kind::Ignored:
                if (index.column() == 0) return entry.value("name").toString(entry.value("id").toString(entry.value("path").toString()));
                if (index.column() == 1) return entry.value("kind").toString();
                if (index.column() == 2) return displayTargets(entry);
                if (index.column() == 3) return entry.value("status").toString();
                break;
        }
        return {};
    }
    void setEntries(const QJsonArray& entries)
    {
        beginResetModel();
        m_entries = {};
        if (m_kind == Kind::Ignored) {
            for (const auto& value : entries) {
                auto entry = value.toObject();
                if (entry.value("status").toString() == QStringLiteral("ignored")) {
                    entry.insert("kind", entry.value("path").isString() ? QStringLiteral("File") : QStringLiteral("Mod"));
                    m_entries.append(entry);
                }
            }
        } else {
            m_entries = entries;
        }
        endResetModel();
    }
    QJsonObject entryAt(int row) const { return row >= 0 && row < m_entries.size() ? m_entries.at(row).toObject() : QJsonObject(); }
   private:
    Kind m_kind;
    QStringList m_headers;
    QJsonArray m_entries;
};

PackEditorPage::PackEditorPage(BaseInstance* instance, QWidget* parent)
    : QWidget(parent), m_instance(instance), m_minecraftInstance(dynamic_cast<MinecraftInstance*>(instance))
{
    m_modsModel = m_minecraftInstance ? m_minecraftInstance->loaderModList() : nullptr;

    auto* outer = new QVBoxLayout(this);
    m_headerLayout = new QGridLayout;
    m_pageTitle = new QLabel(tr("Author workspace"), this);
    m_headerLayout->addWidget(m_pageTitle, 0, 0);
    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_headerLayout->addWidget(m_status, 0, 1);
    m_refreshButton = new QPushButton(tr("Refresh"), this);
    m_scanButton = new QPushButton(tr("Scan changes"), this);
    m_previewButton = new QPushButton(tr("Review changes"), this);
    m_promoteLockButton = new QPushButton(tr("Move schema 3 lock to instance root"), this);
    m_promoteLockButton->setVisible(false);
    m_headerLayout->addWidget(m_refreshButton, 0, 2);
    m_headerLayout->addWidget(m_promoteLockButton, 0, 3);
    m_headerLayout->addWidget(m_scanButton, 0, 4);
    m_headerLayout->addWidget(m_previewButton, 0, 5);
    m_headerLayout->setColumnStretch(1, 1);
    outer->addLayout(m_headerLayout);

    auto* tabs = new QTabWidget(this);
    auto* modsTab = new QWidget(tabs);
    auto* modsLayout = new QVBoxLayout(modsTab);
    m_inventoryToolbar = new QGridLayout;
    m_search = new QLineEdit(modsTab);
    m_search->setPlaceholderText(tr("Search mods by name, file, source or ID"));
    m_search->setAccessibleName(tr("Search mods"));
    m_search->setClearButtonEnabled(true);
    m_targetFilter = new QComboBox(modsTab);
    m_targetFilter->addItem(tr("All targets"), QStringLiteral("all"));
    m_targetFilter->addItem(tr("Client"), QStringLiteral("client"));
    m_targetFilter->addItem(tr("Server"), QStringLiteral("server"));
    m_targetFilter->addItem(tr("Shared"), QStringLiteral("shared"));
    m_stateFilter = new QComboBox(modsTab);
    m_stateFilter->addItem(tr("All states"), QStringLiteral("all"));
    m_stateFilter->addItem(tr("Changed"), QStringLiteral("changed"));
    m_stateFilter->addItem(tr("Excluded"), QStringLiteral("excluded"));
    m_addModButton = new QPushButton(tr("Add mod"), modsTab);
    m_updateModButton = new QPushButton(tr("Choose version"), modsTab);
    m_inventoryToolbar->addWidget(m_search, 0, 0, 1, 2);
    m_inventoryToolbar->addWidget(m_targetFilter, 0, 2);
    m_inventoryToolbar->addWidget(m_stateFilter, 0, 3);
    m_inventoryToolbar->addWidget(m_addModButton, 0, 4);
    m_inventoryToolbar->setColumnStretch(0, 1);
    modsLayout->addLayout(m_inventoryToolbar);
    m_inventorySummary = new QLabel(modsTab);
    m_inventorySummary->setAccessibleName(tr("Inventory result count"));
    modsLayout->addWidget(m_inventorySummary);

    m_inventorySplitter = new QSplitter(Qt::Horizontal, modsTab);
    m_inventoryView = new QTableView(m_inventorySplitter);
    m_inventoryModel = new PackEditorInventoryModel(this);
    m_inventoryFilter = new PackEditorInventoryFilter(this);
    m_inventoryFilter->setSourceModel(m_inventoryModel);
    m_inventoryView->setModel(m_inventoryFilter);
    m_inventoryView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_inventoryView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_inventoryView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_inventoryView->setSortingEnabled(true);
    m_inventoryView->setAlternatingRowColors(true);
    m_inventoryView->verticalHeader()->hide();
    m_inventoryView->verticalHeader()->setDefaultSectionSize(48);
    m_inventoryView->horizontalHeader()->setStretchLastSection(true);
    m_inventoryView->setIconSize(QSize(32, 32));
    m_inventoryView->setContextMenuPolicy(Qt::CustomContextMenu);
    auto* detailPane = new QWidget;
    auto* detailLayout = new QVBoxLayout(detailPane);
    detailLayout->setContentsMargins(12, 8, 8, 8);
    m_selectedDetails = new QLabel(tr("Select a mod to see its details and actions."), detailPane);
    m_selectedDetails->setWordWrap(true);
    m_selectedDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_selectedDetails);
    m_technicalToggle = new QToolButton(detailPane);
    m_technicalToggle->setText(tr("Technical details"));
    m_technicalToggle->setCheckable(true);
    m_technicalToggle->setChecked(false);
    m_technicalToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_technicalToggle->setArrowType(Qt::RightArrow);
    m_technicalDetails = new QLabel(detailPane);
    m_technicalDetails->setWordWrap(true);
    m_technicalDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_technicalDetails->hide();
    detailLayout->addWidget(m_technicalToggle);
    detailLayout->addWidget(m_technicalDetails);
    connect(m_technicalToggle, &QToolButton::toggled, this, [this](bool expanded) {
        m_technicalToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_technicalDetails->setVisible(expanded);
    });
    auto* targetsLabel = new QLabel(tr("Included on"), detailPane);
    detailLayout->addWidget(targetsLabel);
    m_targetClient = new QCheckBox(tr("Client"), detailPane);
    m_targetServer = new QCheckBox(tr("Server"), detailPane);
    m_targetClient->setEnabled(false);
    m_targetServer->setEnabled(false);
    detailLayout->addWidget(m_targetClient);
    detailLayout->addWidget(m_targetServer);
    m_applyTargetsButton = new QPushButton(tr("Apply targets"), detailPane);
    m_applyTargetsButton->setEnabled(false);
    detailLayout->addWidget(m_applyTargetsButton);
    auto* actions = new QVBoxLayout;
    m_ignoreButton = new QPushButton(tr("Exclude from pack"), detailPane);
    m_unmanageButton = new QPushButton(tr("Stop managing"), detailPane);
    m_removeResourceButton = new QPushButton(tr("Remove from pack"), detailPane);
    for (auto* button : {m_updateModButton, m_ignoreButton, m_unmanageButton, m_removeResourceButton}) actions->addWidget(button);
    detailLayout->addLayout(actions);
    detailLayout->addStretch();
    auto* detailScroll = new QScrollArea(m_inventorySplitter);
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    detailScroll->setWidget(detailPane);
    m_inventorySplitter->addWidget(m_inventoryView);
    m_inventorySplitter->addWidget(detailScroll);
    m_inventorySplitter->setStretchFactor(0, 7);
    m_inventorySplitter->setStretchFactor(1, 3);
    modsLayout->addWidget(m_inventorySplitter, 1);
    tabs->addTab(modsTab, tr("Mods"));

    auto* filesTab = new QWidget(tabs);
    auto* filesLayout = new QVBoxLayout(filesTab);
    m_filesTable = new QTableView(filesTab);
    m_filesModel = new PackEditorTableModel(PackEditorTableModel::Kind::Files,
                                             {tr("Path"), tr("Pack file"), tr("Included on"), tr("Update behavior"), tr("Status")}, m_filesTable);
    m_filesTable->setModel(m_filesModel);
    m_filesTable->horizontalHeader()->setStretchLastSection(true);
    m_filesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_filesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    filesLayout->addWidget(m_filesTable);
    auto* fileActions = new QHBoxLayout;
    m_ignoreFileButton = new QPushButton(tr("Ignore"), filesTab);
    m_unmanageFileButton = new QPushButton(tr("Stop managing"), filesTab);
    m_removeFileButton = new QPushButton(tr("Remove"), filesTab);
    fileActions->addWidget(m_ignoreFileButton);
    fileActions->addWidget(m_unmanageFileButton);
    fileActions->addWidget(m_removeFileButton);
    fileActions->addStretch();
    filesLayout->addLayout(fileActions);
    tabs->addTab(filesTab, tr("Managed Files"));

    auto* trackedTab = new QWidget(tabs);
    auto* trackedLayout = new QVBoxLayout(trackedTab);
    m_trackedTable = new QTableView(trackedTab);
    m_trackedModel = new PackEditorTableModel(PackEditorTableModel::Kind::Tracked,
                                               {tr("Path"), tr("Targets"), tr("Policy"), tr("State")}, m_trackedTable);
    m_trackedTable->setModel(m_trackedModel);
    m_trackedTable->horizontalHeader()->setStretchLastSection(true);
    m_trackedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trackedTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* trackedActions = new QHBoxLayout;
    m_addTrackedButton = new QPushButton(tr("Add path"), trackedTab);
    m_removeTrackedButton = new QPushButton(tr("Stop tracking path"), trackedTab);
    trackedActions->addWidget(m_addTrackedButton);
    trackedActions->addWidget(m_removeTrackedButton);
    trackedActions->addStretch();
    trackedLayout->addWidget(m_trackedTable);
    trackedLayout->addLayout(trackedActions);
    tabs->addTab(trackedTab, tr("Watch for files"));

    auto* ignoredTab = new QWidget(tabs);
    auto* ignoredLayout = new QVBoxLayout(ignoredTab);
    m_ignoredTable = new QTableView(ignoredTab);
    m_ignoredModel = new PackEditorTableModel(PackEditorTableModel::Kind::Ignored,
                                               {tr("Resource"), tr("Kind"), tr("Targets"), tr("Status")}, m_ignoredTable);
    m_ignoredTable->setModel(m_ignoredModel);
    m_ignoredTable->horizontalHeader()->setStretchLastSection(true);
    m_ignoredTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ignoredTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ignoredLayout->addWidget(m_ignoredTable);
    auto* excludedInfo = new QLabel(
        tr("Excluded entries remain on this computer and are omitted from the pack. This editor cannot restore an excluded entry yet; remove its matching exclusion rule from ModLock author settings before adding or tracking it again."),
        ignoredTab);
    excludedInfo->setWordWrap(true);
    excludedInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ignoredLayout->addWidget(excludedInfo);
    tabs->addTab(ignoredTab, tr("Excluded"));
    outer->addWidget(tabs, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &PackEditorPage::loadAuthorState);
    connect(m_promoteLockButton, &QPushButton::clicked, this, [this] {
        const auto confirmation = QMessageBox::warning(this, tr("Move the author workspace"),
            tr("This moves the Schema 3 pack lock from the shared game folder into this instance. A backup of the current lock will be kept. Other instances will continue using the original workspace."),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
        if (confirmation != QMessageBox::Ok) return;
        runOperation("promote-schema3-lock", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}},
                     [this](const QJsonObject& result) {
                         showStatusMessage(tr("Moved ModLock workspace to the instance root. Previous lock backup: %1").arg(result.value("backup").toString()));
                         loadAuthorState();
                     });
    });
    connect(m_scanButton, &QPushButton::clicked, this, [this] { runOperation("author-scan", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}}); });
    connect(m_previewButton, &QPushButton::clicked, this, &PackEditorPage::showPublishPreview);
    const auto updateInventorySummary = [this] {
        const int visibleCount = m_inventoryFilter->rowCount();
        if (visibleCount > 0) m_inventorySummary->setText(tr("%n mod(s)", "Visible inventory count", visibleCount));
        else if (m_state.mods.isEmpty()) m_inventorySummary->setText(tr("No mods in this pack. Add a mod to start building the inventory."));
        else if (!m_search->text().isEmpty()) m_inventorySummary->setText(tr("No mods match your search."));
        else m_inventorySummary->setText(tr("No mods match the selected filters."));
        if (!m_inventoryView->currentIndex().isValid() && !m_retainedSelection.isEmpty()) {
            const int row = m_inventoryModel->rowForResourceKey(m_inventoryModel->resourceKey(m_retainedSelection));
            const auto index = row >= 0 ? m_inventoryFilter->mapFromSource(m_inventoryModel->index(row, 0)) : QModelIndex();
            if (index.isValid()) m_inventoryView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            else showSelectedMod(m_retainedSelection);
        }
    };
    connect(m_search, &QLineEdit::textChanged, this, [this, updateInventorySummary](const QString& text) {
        m_inventoryFilter->setSearchText(text);
        updateInventorySummary();
    });
    connect(m_targetFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, updateInventorySummary](int index) {
        m_inventoryFilter->setTargetFilter(m_targetFilter->itemData(index).toString());
        updateInventorySummary();
    });
    connect(m_stateFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, updateInventorySummary](int index) {
        m_inventoryFilter->setStateFilter(m_stateFilter->itemData(index).toString());
        updateInventorySummary();
    });
    connect(m_inventoryView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { requestVisibleIcons(); });
    connect(m_addModButton, &QPushButton::clicked, this, [this] { addMods(); });
    connect(m_updateModButton, &QPushButton::clicked, this, [this] { addMods(selectedMod()); });
    connect(m_applyTargetsButton, &QPushButton::clicked, this, [this] {
        QJsonArray targets;
        if (m_targetClient->isChecked()) targets.append(QStringLiteral("client"));
        if (m_targetServer->isChecked()) targets.append(QStringLiteral("server"));
        setSelectedTargets(targets);
    });
    const auto updateTargetApply = [this] {
        const auto mod = selectedMod();
        const QJsonArray current = mod.value("targets").toArray();
        QJsonArray selected;
        if (m_targetClient->isChecked()) selected.append(QStringLiteral("client"));
        if (m_targetServer->isChecked()) selected.append(QStringLiteral("server"));
        m_applyTargetsButton->setEnabled(!m_bridge && !mod.isEmpty() && packEditorTargetsAreValid(selected) && selected != current);
    };
    connect(m_targetClient, &QCheckBox::toggled, this, updateTargetApply);
    connect(m_targetServer, &QCheckBox::toggled, this, updateTargetApply);
    connect(m_ignoreButton, &QPushButton::clicked, this, [this] { setSelectedResourceState("ignored"); });
    connect(m_unmanageButton, &QPushButton::clicked, this, [this] { setSelectedResourceState("unmanaged"); });
    connect(m_removeResourceButton, &QPushButton::clicked, this, &PackEditorPage::removeSelectedResource);
    connect(m_ignoreFileButton, &QPushButton::clicked, this, [this] { setSelectedFileState("ignored"); });
    connect(m_unmanageFileButton, &QPushButton::clicked, this, [this] { setSelectedFileState("unmanaged"); });
    connect(m_removeFileButton, &QPushButton::clicked, this, &PackEditorPage::removeSelectedFile);
    connect(m_addTrackedButton, &QPushButton::clicked, this, &PackEditorPage::addTrackedPath);
    connect(m_removeTrackedButton, &QPushButton::clicked, this, &PackEditorPage::removeTrackedPath);
    connect(m_inventoryView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex&, const QModelIndex&) {
                const auto mod = selectedMod();
                if (!mod.isEmpty()) {
                    m_retainedSelection = mod;
                    showSelectedMod(mod);
                }
            });
    connect(m_inventoryView, &QTableView::customContextMenuRequested, this, [this](const QPoint& point) {
        const auto index = m_inventoryView->indexAt(point);
        if (!index.isValid()) return;
        m_inventoryView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        const auto mod = selectedMod();
        QMenu menu(this);
        auto* update = menu.addAction(m_updateModButton->text(), this, [this] { addMods(selectedMod()); });
        update->setEnabled(m_state.lock.toObject().value("schema").toInt() == 3 && !mod.value("project_id").toString().isEmpty());
        menu.addAction(tr("Exclude from pack"), this, [this] { setSelectedResourceState(QStringLiteral("ignored")); });
        auto* unmanage = menu.addAction(tr("Stop managing"), this, [this] { setSelectedResourceState(QStringLiteral("unmanaged")); });
        unmanage->setEnabled(mod.value("managed").toBool());
        menu.addSeparator();
        auto* remove = menu.addAction(tr("Remove from pack…"), this, &PackEditorPage::removeSelectedResource);
        remove->setEnabled(mod.value("managed").toBool());
        menu.exec(m_inventoryView->viewport()->mapToGlobal(point));
    });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this] { m_search->setFocus(); });
    connect(m_filesTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        const bool selected = m_filesTable->currentIndex().isValid();
        m_ignoreFileButton->setEnabled(selected);
        m_unmanageFileButton->setEnabled(selected);
        m_removeFileButton->setEnabled(selected && m_state.lock.toObject().value("schema").toInt() == 3);
    });
    m_ignoreFileButton->setEnabled(false);
    m_unmanageFileButton->setEnabled(false);
    m_removeFileButton->setEnabled(false);

    if (m_modsModel) {
        const auto scheduleMetadataRefresh = [this] {
            QTimer::singleShot(120, this, [this] {
                if (applyLauncherMetadata()) {
                    refreshView();
                    resolveProviderMetadata();
                }
            });
        };
        connect(m_modsModel, &ResourceFolderModel::updateFinished, this, scheduleMetadataRefresh);
        connect(m_modsModel, &ResourceFolderModel::parseFinished, this, scheduleMetadataRefresh);
    }
}

void PackEditorPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const bool compact = width() < 980;
    if (compact == m_compactLayout) return;
    m_compactLayout = compact;
    m_inventorySplitter->setOrientation(compact ? Qt::Vertical : Qt::Horizontal);
    m_inventoryToolbar->removeWidget(m_search);
    m_inventoryToolbar->removeWidget(m_targetFilter);
    m_inventoryToolbar->removeWidget(m_stateFilter);
    m_inventoryToolbar->removeWidget(m_addModButton);
    for (auto* widget : {m_pageTitle, m_status, m_refreshButton, m_promoteLockButton, m_scanButton, m_previewButton})
        m_headerLayout->removeWidget(widget);
    if (compact) {
        m_headerLayout->addWidget(m_pageTitle, 0, 0);
        m_headerLayout->addWidget(m_status, 0, 1, 1, 2);
        m_headerLayout->addWidget(m_refreshButton, 1, 0);
        m_headerLayout->addWidget(m_scanButton, 1, 1);
        m_headerLayout->addWidget(m_promoteLockButton, 2, 0, 1, 2);
        m_headerLayout->addWidget(m_previewButton, 3, 0, 1, 2);
        m_inventoryToolbar->addWidget(m_search, 0, 0, 1, 3);
        m_inventoryToolbar->addWidget(m_targetFilter, 1, 0);
        m_inventoryToolbar->addWidget(m_stateFilter, 1, 1);
        m_inventoryToolbar->addWidget(m_addModButton, 1, 2);
    } else {
        m_headerLayout->addWidget(m_pageTitle, 0, 0);
        m_headerLayout->addWidget(m_status, 0, 1);
        m_headerLayout->addWidget(m_refreshButton, 0, 2);
        m_headerLayout->addWidget(m_promoteLockButton, 0, 3);
        m_headerLayout->addWidget(m_scanButton, 0, 4);
        m_headerLayout->addWidget(m_previewButton, 0, 5);
        m_inventoryToolbar->addWidget(m_search, 0, 0, 1, 2);
        m_inventoryToolbar->addWidget(m_targetFilter, 0, 2);
        m_inventoryToolbar->addWidget(m_stateFilter, 0, 3);
        m_inventoryToolbar->addWidget(m_addModButton, 0, 4);
    }
}

QIcon PackEditorPage::icon() const { return QIcon::fromTheme(QStringLiteral("modlock")); }

bool PackEditorPage::shouldDisplay() const
{
    if (!m_minecraftInstance) return false;
    const bool hasWorkspace = QFileInfo::exists(QDir(m_minecraftInstance->instanceRoot()).filePath(QStringLiteral("mod.lock")));
    const bool canPromote = QFileInfo::exists(QDir(m_minecraftInstance->gameRoot()).filePath(QStringLiteral("mod.lock")));
    return packEditorPageShouldDisplay(true, hasWorkspace, canPromote);
}

bool PackEditorPage::prepareToClose()
{
    if (!m_bridge || !m_bridge->isActive())
        return true;
    QMessageBox box(QMessageBox::Warning, tr("Pack Editor operation is running"),
                    tr("Wait for the current ModLock operation or cancel it cooperatively."), QMessageBox::NoButton, this);
    auto* wait = box.addButton(tr("Wait"), QMessageBox::RejectRole);
    auto* cancel = box.addButton(tr("Cancel operation"), QMessageBox::DestructiveRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(wait));
    box.exec();
    if (box.clickedButton() == cancel)
        m_bridge->cancel();
    return false;
}

void PackEditorPage::openedImpl()
{
    if (m_modsModel && !m_launcherMetadataLoadRequested) {
        m_launcherMetadataLoadRequested = true;
        if (m_modsModel->rowCount() == 0)
            m_modsModel->update();
    }
    loadAuthorState();
}

void PackEditorPage::loadAuthorState()
{
    if (!shouldDisplay() || m_bridge)
        return;
    const auto lockPath = QDir(m_minecraftInstance->instanceRoot()).filePath(QStringLiteral("mod.lock"));
    m_hasWorkspace = QFileInfo::exists(lockPath);
    m_promoteLockButton->setVisible(!m_hasWorkspace && QFileInfo::exists(QDir(m_minecraftInstance->gameRoot()).filePath(QStringLiteral("mod.lock"))));
    if (!m_hasWorkspace) {
        m_state = {};
        m_retainedSelection = {};
        showSelectedMod({});
        m_inventoryModel->setEntries({});
        m_filesModel->setEntries({});
        m_trackedModel->setEntries({});
        m_ignoredModel->setEntries({});
        for (auto* button : {m_scanButton, m_previewButton, m_addModButton, m_updateModButton,
                             m_ignoreButton, m_unmanageButton,
                             m_removeResourceButton, m_addTrackedButton, m_removeTrackedButton, m_ignoreFileButton,
                             m_unmanageFileButton, m_removeFileButton})
            button->setEnabled(false);
        m_status->setText(tr("No ModLock workspace at the instance root. Import a schema 3 pack to manage client and server together."));
        m_inventorySummary->setText(tr("No pack workspace is set up for this instance."));
        return;
    }
    for (auto* button : {m_scanButton, m_previewButton, m_addModButton, m_updateModButton,
                         m_ignoreButton, m_unmanageButton,
                         m_removeResourceButton, m_addTrackedButton, m_removeTrackedButton, m_ignoreFileButton,
                         m_unmanageFileButton, m_removeFileButton})
        button->setEnabled(true);
    runOperation("author-state", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}});
}

void PackEditorPage::refreshView()
{
    applyLauncherMetadata();
    const auto previousSelection = selectedMod().isEmpty() ? m_retainedSelection : selectedMod();
    const QString selectedResourceKey = m_inventoryModel->resourceKey(previousSelection);
    const int scrollPosition = m_inventoryView->verticalScrollBar()->value();
    m_inventoryModel->setEntries(m_state.mods);
    m_filesModel->setEntries(m_state.files);
    m_trackedModel->setEntries(m_state.trackedPaths);
    QJsonArray ignored;
    for (const auto& item : m_state.mods) ignored.append(item);
    for (const auto& item : m_state.files) ignored.append(item);
    for (const auto& item : m_state.trackedPaths) {
        if (item.toObject().value("excluded").toBool()) ignored.append(item);
    }
    m_ignoredModel->setEntries(ignored);
    m_status->setText(tr("Branch: %1%2").arg(m_state.branch, m_state.dirty ? tr(" — local changes") : QString()));
    const int visibleCount = m_inventoryFilter->rowCount();
    if (visibleCount > 0) m_inventorySummary->setText(tr("%n mod(s)", "Visible inventory count", visibleCount));
    else if (m_state.mods.isEmpty()) m_inventorySummary->setText(tr("No mods in this pack. Add a mod to start building the inventory."));
    else if (!m_search->text().isEmpty()) m_inventorySummary->setText(tr("No mods match your search."));
    else m_inventorySummary->setText(tr("No mods match the selected filters."));
    if (!selectedResourceKey.isEmpty()) {
        const int sourceRow = m_inventoryModel->rowForResourceKey(selectedResourceKey);
        if (sourceRow >= 0) {
            const auto proxyIndex = m_inventoryFilter->mapFromSource(m_inventoryModel->index(sourceRow, 0));
            if (proxyIndex.isValid()) {
                m_inventoryView->selectionModel()->setCurrentIndex(proxyIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                m_inventoryView->scrollTo(proxyIndex, QAbstractItemView::EnsureVisible);
            } else {
                m_retainedSelection = m_inventoryModel->entryAt(sourceRow);
                showSelectedMod(m_retainedSelection);
            }
        } else {
            m_retainedSelection = {};
            showSelectedMod({});
        }
    }
    m_inventoryView->verticalScrollBar()->setValue(scrollPosition);
    QTimer::singleShot(0, this, [this] { requestVisibleIcons(); });
}

void PackEditorPage::requestVisibleIcons()
{
    if (!m_inventoryView || !m_inventoryView->viewport()) return;
    constexpr int step = 48;
    for (int y = 0; y < m_inventoryView->viewport()->height(); y += step) {
        const auto index = m_inventoryView->indexAt(QPoint(2, y));
        if (index.isValid()) requestModIcon(m_inventoryFilter->mapToSource(index).data(PackEditorInventoryModel::EntryRole).toJsonObject());
    }
}

void PackEditorPage::requestModIcon(const QJsonObject& mod)
{
    const QString identity = mod.value("identity").toString(mod.value("id").toString());
    const QString url = mod.value("icon_url").toString();
    if (identity.isEmpty())
        return;
    if (url.isEmpty()) return;
    if (m_iconCache.contains(url)) {
        m_inventoryModel->setIcon(identity, m_iconCache.value(url));
        return;
    }
    if (m_iconRequests.contains(url)) return;
    const QUrl parsed(url);
    if (parsed.scheme() != QStringLiteral("https") || parsed.host().isEmpty())
        return;
    m_iconRequests.insert(url);
    auto* reply = APPLICATION->network()->get(QNetworkRequest(parsed));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, identity] {
        m_iconRequests.remove(url);
        const QByteArray bytes = reply->readAll();
        if (reply->error() == QNetworkReply::NoError && bytes.size() <= 1024 * 1024) {
            QPixmap pixmap;
            if (pixmap.loadFromData(bytes) && !pixmap.isNull()) {
                QIcon icon(pixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_iconCache.insert(url, icon);
                for (const auto& value : m_state.mods) {
                    const auto entry = value.toObject();
                    if (entry.value("icon_url").toString() == url) {
                        const auto key = entry.value("identity").toString(entry.value("id").toString());
                        m_inventoryModel->setIcon(key, icon);
                    }
                }
            }
        }
        reply->deleteLater();
    });
}

bool PackEditorPage::applyLauncherMetadata()
{
    if (!m_modsModel || m_state.mods.isEmpty())
        return false;

    QHash<QString, Mod*> modsByProviderId;
    QHash<QString, Mod*> modsByFilename;
    for (auto* mod : m_modsModel->allMods()) {
        if (!mod)
            continue;
        if (const auto metadata = mod->metadata(); metadata) {
            const QString provider = QString::fromLatin1(ModPlatform::ProviderCapabilities::name(metadata->provider));
            const QString projectId = metadata->project_id.toString();
            if (!provider.isEmpty() && !projectId.isEmpty())
                modsByProviderId.insert(provider + QLatin1Char(':') + projectId, mod);
        }
        QString filename = mod->fileinfo().fileName();
        if (filename.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
            filename.chop(QStringLiteral(".disabled").size());
        if (!filename.isEmpty())
            modsByFilename.insert(filename, mod);
    }

    bool changed = false;
    for (int i = 0; i < m_state.mods.size(); ++i) {
        auto entry = m_state.mods.at(i).toObject();
        bool entryChanged = false;
        const QString source = entry.value("source").toString(entry.value("provider").toString()).toLower();
        const QString projectId = entry.value("project_id").toString();
        const QString filename = entry.value("filename").toString();
        Mod* mod = nullptr;
        if (!source.isEmpty() && !projectId.isEmpty())
            mod = modsByProviderId.value(source + QLatin1Char(':') + projectId, nullptr);
        if (!mod && !filename.isEmpty())
            mod = modsByFilename.value(filename, nullptr);
        if (!mod)
            continue;

        const QString identity = entry.value("identity").toString(entry.value("id").toString());
        const QString name = mod->name().trimmed();
        if (!name.isEmpty() && (entry.value("name").toString().isEmpty() || entry.value("name").toString() == entry.value("id").toString())) {
            entry.insert("name", name);
            entryChanged = true;
        }
        if (filename.isEmpty() && mod->metadata() && !mod->metadata()->filename.isEmpty()) {
            entry.insert("filename", mod->metadata()->filename);
            entryChanged = true;
        }

        const auto iconPixmap = mod->icon(QSize(32, 32), Qt::KeepAspectRatio);
        if (!identity.isEmpty() && !iconPixmap.isNull() && !m_launcherCachedIconIdentities.contains(identity)) {
            const QIcon icon(iconPixmap);
            m_inventoryModel->setIcon(identity, icon);
            m_launcherCachedIconIdentities.insert(identity);
            changed = true;
        }
        if (entryChanged) {
            m_state.mods.replace(i, entry);
            changed = true;
        }
    }
    return changed;
}

void PackEditorPage::resolveProviderMetadata()
{
    QStringList modrinthIds;
    QStringList flameIds;
    for (const auto& value : m_state.mods) {
        const auto mod = value.toObject();
        if (!mod.value("name").toString().isEmpty() && !mod.value("icon_url").toString().isEmpty())
            continue;
        const QString source = mod.value("source").toString(mod.value("provider").toString()).toLower();
        const QString projectId = mod.value("project_id").toString();
        if (projectId.isEmpty())
            continue;
        const QString identity = mod.value("identity").toString(mod.value("id").toString());
        if (m_launcherCachedIconIdentities.contains(identity))
            continue;
        const QString requestKey = source + QLatin1Char(':') + projectId;
        if (m_metadataRequested.contains(requestKey))
            continue;
        if (source == QStringLiteral("modrinth")) {
            modrinthIds.append(projectId);
            m_metadataRequested.insert(requestKey);
        } else if (source == QStringLiteral("curseforge")) {
            flameIds.append(projectId);
            m_metadataRequested.insert(requestKey);
        }
    }

    static ModrinthAPI modrinthApi;
    static FlameAPI flameApi;
    const auto startBatches = [this](QStringList ids, int batchSize, bool curseforge) {
        for (int offset = 0; offset < ids.size(); offset += batchSize) {
            const auto batch = ids.mid(offset, batchSize);
            Task::Ptr task;
            QByteArray* response = nullptr;
            if (curseforge)
                std::tie(task, response) = flameApi.getProjects(batch);
            else
                std::tie(task, response) = modrinthApi.getProjects(batch);
            if (!task || !response)
                continue;

            auto* taskObject = task.get();
            m_metadataTasks.append(task);
            connect(taskObject, &Task::finished, this, [this, taskObject, response, batch, curseforge] {
                const bool success = taskObject->wasSuccessful();
                if (!success) {
                    QTimer::singleShot(0, this, [this, taskObject] {
                        for (auto it = m_metadataTasks.begin(); it != m_metadataTasks.end(); ++it) {
                            if (it->get() == taskObject) { m_metadataTasks.erase(it); break; }
                        }
                    });
                    return;
                }

                QJsonParseError parseError;
                const auto document = QJsonDocument::fromJson(*response, &parseError);
                if (parseError.error != QJsonParseError::NoError)
                    return;
                QJsonArray entries;
                if (curseforge) {
                    entries = document.object().value("data").toArray();
                    if (entries.isEmpty() && batch.size() == 1 && document.object().value("data").isObject())
                        entries.append(document.object().value("data"));
                } else if (document.isArray()) {
                    entries = document.array();
                } else if (document.isObject()) {
                    entries.append(document.object());
                }

                bool changed = false;
                for (const auto& item : entries) {
                    auto object = item.toObject();
                    ModPlatform::IndexedPack pack;
                    try {
                        if (curseforge)
                            FlameMod::loadIndexedPack(pack, object);
                        else
                            Modrinth::loadIndexedPack(pack, object);
                    } catch (...) {
                        continue;
                    }
                    const QString projectId = pack.addonId.toString();
                    for (int i = 0; i < m_state.mods.size(); ++i) {
                        auto entry = m_state.mods.at(i).toObject();
                        const QString source = entry.value("source").toString(entry.value("provider").toString()).toLower();
                        if (entry.value("project_id").toString() != projectId || source != (curseforge ? QStringLiteral("curseforge") : QStringLiteral("modrinth")))
                            continue;
                        if (entry.value("name").toString().isEmpty() || entry.value("name").toString() == entry.value("id").toString())
                            entry.insert("name", pack.name);
                        if (entry.value("icon_url").toString().isEmpty())
                            entry.insert("icon_url", pack.logoUrl);
                        m_state.mods.replace(i, entry);
                        changed = true;
                    }
                }
                if (changed)
                    refreshView();
                QTimer::singleShot(0, this, [this, taskObject] {
                    for (auto it = m_metadataTasks.begin(); it != m_metadataTasks.end(); ++it) {
                        if (it->get() == taskObject) { m_metadataTasks.erase(it); break; }
                    }
                });
            });
            task->start();
        }
    };
    startBatches(modrinthIds, 50, false);
    startBatches(flameIds, 10, true);
}

void PackEditorPage::setBusy(bool busy)
{
    m_refreshButton->setEnabled(!busy);
    m_promoteLockButton->setEnabled(!busy && m_promoteLockButton->isVisible());
    const bool schemaThree = m_state.lock.toObject().value("schema").toInt() == 3;
    for (auto* button : {m_scanButton, m_previewButton, m_addModButton,
                         m_ignoreButton, m_unmanageButton,
                         m_removeResourceButton, m_addTrackedButton, m_removeTrackedButton, m_updateModButton,
                         m_ignoreFileButton, m_unmanageFileButton, m_removeFileButton})
        button->setEnabled(!busy && m_hasWorkspace);
    m_previewButton->setEnabled(!busy && m_hasWorkspace && schemaThree);
    m_addModButton->setEnabled(!busy && m_hasWorkspace && schemaThree);
    m_inventoryView->setEnabled(!busy && m_hasWorkspace);
    m_targetClient->setEnabled(false);
    m_targetServer->setEnabled(false);
    m_applyTargetsButton->setEnabled(false);
    if (!busy) showSelectedMod(selectedMod());
}

void PackEditorPage::runOperation(const QString& operation,
                                  const QJsonObject& params,
                                  std::function<void(const QJsonObject&)> onSuccess)
{
    if (m_bridge || !m_minecraftInstance)
        return;
    if (operation != QStringLiteral("publish-preview") && operation != QStringLiteral("publish")) {
        m_previewId.clear();
    }
    m_operation = operation;
    m_result = {};
    m_error = {};
    m_onSuccess = std::move(onSuccess);
    setBusy(true);
    m_bridge = new ModLockBridge(m_minecraftInstance->instanceRoot(), this);
    connect(m_bridge, &ModLockBridge::progress, this, [this](const QString&, const QString& message) { m_status->setText(message); });
    connect(m_bridge, &ModLockBridge::completed, this, [this](const QString&, const QJsonObject& result) { m_result = result; });
    connect(m_bridge, &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) { m_error = error; });
    connect(m_bridge, &ModLockBridge::finished, this, &PackEditorPage::onOperationFinished);
    if (!m_bridge->start(operation, params)) {
        m_bridge->deleteLater();
        m_bridge = nullptr;
        m_onSuccess = {};
        setBusy(false);
        showStatusMessage(tr("Could not start the ModLock component."), true);
    }
}

void PackEditorPage::onOperationFinished()
{
    if (m_bridge) {
        m_bridge->deleteLater();
        m_bridge = nullptr;
    }
    setBusy(false);
    if (!m_error.isEmpty()) {
        m_onSuccess = {};
        const auto errorDetails = m_error.value("details").toObject();
        const QString conflictKind = m_error.value("kind").toString(errorDetails.value("kind").toString());
        if (m_operation == QStringLiteral("publish") && conflictKind == QStringLiteral("changed_during_apply")) {
            m_previewId.clear();
        }
        if (m_operation == QStringLiteral("publish") && m_error.value("code").toString() == QStringLiteral("push_failed")) {
            const auto details = errorDetails;
            const auto commit = details.value("commit").toString();
            const auto branch = details.value("branch").toString();
            QString message = tr("The local commit was created, but pushing it to the remote did not complete. No automatic retry was attempted.");
            if (!commit.isEmpty())
                message += tr("\nCommit: %1").arg(commit);
            if (!branch.isEmpty())
                message += tr("\nBranch: %1").arg(branch);
            m_previewId.clear();
            showStatusMessage(message, true);
            loadAuthorState();
            return;
        }
        showStatusMessage(m_error.value("message").toString(tr("ModLock operation failed.")), true);
        return;
    }
    if (m_operation == QStringLiteral("author-state")) {
        QString error;
        if (!PackEditorAuthorState::parse(m_result, &m_state, &error)) {
            showStatusMessage(error, true);
            return;
        }
        refreshView();
        resolveProviderMetadata();
    } else if (m_operation == QStringLiteral("author-scan")) {
        QString error;
        if (!PackEditorAuthorState::parse(m_result, &m_state, &error)) {
            showStatusMessage(error, true);
            return;
        }
        refreshView();
        resolveProviderMetadata();
        showStatusMessage(tr("Scan complete. Resource statuses have been refreshed."));
    } else if (m_operation == QStringLiteral("publish-preview")) {
        m_previewId = m_result.value("preview_id").toString();
        if (m_previewId.isEmpty()) {
            showStatusMessage(tr("ModLock returned a preview without a publish token."), true);
            return;
        }
        PackEditorReviewDialog review(m_result, this);
        if (review.exec() == QDialog::Accepted) publish(review.commitMessage());
    } else if (m_operation == QStringLiteral("publish")) {
        m_previewId.clear();
        if (packEditorPublishResultIsConfirmed(m_result)) {
            showStatusMessage(tr("Published commit %1 to %2.").arg(m_result.value("commit").toString(), m_result.value("branch").toString()));
        } else if (m_result.value("pushed").isBool() && !m_result.value("pushed").toBool() &&
                   !m_result.value("commit").toString().isEmpty()) {
            QString message = tr("Saved locally — not published remotely.");
            message += tr("\nCommit: %1").arg(m_result.value("commit").toString());
            if (!m_result.value("branch").toString().isEmpty())
                message += tr("\nBranch: %1").arg(m_result.value("branch").toString());
            showStatusMessage(message, true);
        } else {
            QString message = tr("ModLock returned an incomplete publication result. Remote publication could not be confirmed.");
            if (!m_result.value("commit").toString().isEmpty())
                message += tr("\nCommit: %1").arg(m_result.value("commit").toString());
            if (!m_result.value("branch").toString().isEmpty())
                message += tr("\nBranch: %1").arg(m_result.value("branch").toString());
            showStatusMessage(message, true);
        }
        loadAuthorState();
    } else if (m_onSuccess) {
        const auto callback = std::move(m_onSuccess);
        callback(m_result);
        return;
    }
    m_onSuccess = {};
}

QJsonObject PackEditorPage::selectedMod() const
{
    if (!m_inventoryView || !m_inventoryView->currentIndex().isValid()) return m_retainedSelection;
    const auto sourceIndex = m_inventoryFilter->mapToSource(m_inventoryView->currentIndex());
    if (!sourceIndex.isValid()) return {};
    return m_inventoryModel->entryAt(sourceIndex.row());
}

void PackEditorPage::showSelectedMod(const QJsonObject& mod)
{
    if (mod.isEmpty()) {
        m_selectedDetails->setText(tr("Select a mod to see its details and actions."));
        m_targetClient->setChecked(false);
        m_targetServer->setChecked(false);
        m_targetClient->setEnabled(false);
        m_targetServer->setEnabled(false);
        m_applyTargetsButton->setEnabled(false);
        m_updateModButton->setEnabled(false);
        m_ignoreButton->setEnabled(false);
        m_unmanageButton->setEnabled(false);
        m_removeResourceButton->setEnabled(false);
        return;
    }
    const QString name = mod.value("name").toString(mod.value("filename").toString(tr("Name unavailable")));
    const QString provider = mod.value("provider").toString(mod.value("source").toString(tr("Unknown source")));
    const QString targets = displayTargets(mod);
    const int modelRow = m_inventoryModel->rowForResourceKey(m_inventoryModel->resourceKey(mod));
    const QString state = modelRow >= 0
        ? m_inventoryModel->index(modelRow, PackEditorInventoryModel::StateColumn).data(Qt::DisplayRole).toString()
        : tr("Unknown");
    const QString technical = tr("Filename: %1\nProject ID: %2\nResource ID: %3\nSHA-256: %4")
                                  .arg(mod.value("filename").toString(), mod.value("project_id").toString(),
                                       mod.value("identity").toString(mod.value("id").toString()), mod.value("sha256").toString());
    m_selectedDetails->setText(tr("%1\nVersion %2 · %3\n\nIncluded on: %4\nPack state: %5\nLocal state: %6")
        .arg(name, mod.value("version").toString(tr("Unknown version")), provider, targets, state,
             packEditorStatusSummary(mod)));
    if (!m_inventoryView->currentIndex().isValid())
        m_selectedDetails->setText(m_selectedDetails->text() + tr("\n\nThis mod is outside the current filters."));
    m_technicalDetails->setText(technical);
    const QSignalBlocker blockClient(m_targetClient);
    const QSignalBlocker blockServer(m_targetServer);
    const auto assigned = targetsFor(mod);
    m_targetClient->setChecked(assigned.contains(QStringLiteral("client")));
    m_targetServer->setChecked(assigned.contains(QStringLiteral("server")));
    const bool selectedVisible = m_inventoryView->currentIndex().isValid();
    const bool schemaThree = selectedVisible && m_state.lock.toObject().value("schema").toInt() == 3 && mod.value("managed").toBool();
    m_targetClient->setEnabled(schemaThree && !m_bridge);
    m_targetServer->setEnabled(schemaThree && !m_bridge);
    m_applyTargetsButton->setEnabled(false);
    const bool managed = mod.value("managed").toBool();
    m_updateModButton->setEnabled(!m_bridge && m_state.lock.toObject().value("schema").toInt() == 3 &&
                                  !mod.value("project_id").toString().isEmpty());
    m_ignoreButton->setEnabled(!m_bridge);
    m_unmanageButton->setEnabled(!m_bridge && managed);
    m_removeResourceButton->setEnabled(!m_bridge && managed && m_state.lock.toObject().value("schema").toInt() == 3);
    if (!m_bridge && m_filesTable->currentIndex().isValid())
        m_removeFileButton->setEnabled(m_state.lock.toObject().value("schema").toInt() == 3);
}

void PackEditorPage::setSelectedTargets(const QJsonArray& targets)
{
    const auto mod = selectedMod();
    if (mod.isEmpty() || !packEditorTargetsAreValid(targets) || m_state.lock.toObject().value("schema").toInt() != 3) return;
    const QString identity = mod.value("identity").toString(mod.value("id").toString());
    runOperation("set-mod-targets", packEditorSetModTargetsParams(identity, targets),
                 [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::setSelectedResourceState(const QString& state)
{
    const auto mod = selectedMod();
    if (mod.isEmpty()) return;
    const QString name = mod.value("name").toString(mod.value("filename").toString());
    const QString consequence = state == QStringLiteral("ignored")
        ? tr("Exclude %1 from the pack? Its installed files will stay on this computer.").arg(name)
        : tr("Stop managing %1? Its installed files will stay on this computer as local only content.").arg(name);
    if (QMessageBox::question(this, state == QStringLiteral("ignored") ? tr("Exclude mod") : tr("Stop managing mod"),
                              consequence, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    runOperation("set-resource-state", {{"kind", "mod"}, {"identity", mod.value("identity")}, {"state", state}},
                 [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::addMods(const QJsonObject& existingMod)
{
    if (!m_minecraftInstance || m_bridge) return;
    auto* dialog = new ResourceDownload::ModDownloadDialog(this, m_minecraftInstance->loaderModList(), m_instance, true);
    const QString projectId = existingMod.value("project_id").toString();
    if (!projectId.isEmpty() && dialog->selectedPage())
        dialog->selectedPage()->openProject(projectId);
    const QStringList defaultTargets = existingMod.isEmpty() ? QStringList{} : targetsFor(existingMod);
    connect(dialog, &QDialog::finished, this, [this, dialog, defaultTargets](int result) {
        QList<ResourceDownload::ResourceDownloadDialog::DownloadTaskPtr> selected;
        if (result == QDialog::Accepted)
            selected = dialog->getTasks();
        dialog->deleteLater();
        for (const auto& task : selected) {
            auto side = task->getVersion().side;
            if (side == ModPlatform::Side::NoSide && task->getPack())
                side = task->getPack()->side;
            const auto initialTargets = defaultTargets.isEmpty() ? targetsForProviderSide(side) : defaultTargets;
            TargetChoiceDialog targets(this, initialTargets);
            if (targets.exec() != QDialog::Accepted)
                continue;
            addSelectedMod(task->getVersion(), task->getPack(), targets.targets());
        }
    });
    dialog->open();
}

void PackEditorPage::addSelectedMod(ModPlatform::IndexedVersion version, ModPlatform::IndexedPack::Ptr pack, const QStringList& targets)
{
    if (!pack) return;
    const QString source = QString::fromLatin1(ModPlatform::ProviderCapabilities::name(pack->provider));
    const QString projectId = pack->addonId.toString();
    const QString identity = source + QStringLiteral(":") + (pack->slug.isEmpty() ? projectId : pack->slug);
    const QJsonObject metadata{{"id", identity},
                               {"identity", identity},
                               {"name", pack->name},
                               {"icon_url", pack->logoUrl},
                               {"version", version.version},
                               {"filename", version.fileName},
                               {"source", source},
                               {"provider", source},
                               {"project_id", projectId},
                               {"version_id", version.fileId.toString()},
                               {"mod_id", projectId},
                               {"file_id", version.fileId.toString()},
                               {"url", version.downloadUrl}};
    auto* task = new ModLockAddModTask(m_minecraftInstance->instanceRoot(), metadata, toTargets(targets),
                                      modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot()), this);
    ProgressDialog progress(this);
    progress.setSkipButton(true, tr("Cancel"));
    const auto result = progress.execWithTask(task);
    task->deleteLater();
    if (result != QDialog::Accepted) {
        showStatusMessage(tr("Adding mod failed or was cancelled."), true);
        return;
    }
    loadAuthorState();
}

void PackEditorPage::setSelectedFileState(const QString& state)
{
    const int row = m_filesTable->currentIndex().row();
    const auto file = m_filesModel->entryAt(row);
    if (file.isEmpty()) return;
    const QString consequence = state == QStringLiteral("ignored")
        ? tr("Exclude %1 from the pack? The local file will stay on this computer.").arg(file.value("target").toString())
        : tr("Stop managing %1? The local file will stay on this computer as local only content.").arg(file.value("target").toString());
    if (QMessageBox::question(this, state == QStringLiteral("ignored") ? tr("Exclude file") : tr("Stop managing file"),
                              consequence, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    runOperation("set-resource-state", {{"kind", "file"}, {"identity", file.value("path")}, {"state", state}},
                 [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::removeSelectedFile()
{
    const int row = m_filesTable->currentIndex().row();
    const auto file = m_filesModel->entryAt(row);
    if (file.isEmpty()) return;
    const auto reply = QMessageBox::warning(this, tr("Remove managed file"),
                                            tr("Remove %1 from the pack and delete the matching local file(s)? ModLock checks the file hashes and stops if a file has changed locally.")
                                                .arg(file.value("target").toString()),
                                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply != QMessageBox::Yes) return;
    QJsonObject params{{"kind", "file"}, {"identity", file.value("path")}, {"targets", file.value("targets")}};
    if (!file.value("sha256").toString().isEmpty()) params.insert("expected_sha256", file.value("sha256"));
    params.insert("target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot()));
    runOperation("remove-resource", params, [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::addTrackedPath()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Track a path"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* path = new QLineEdit(&dialog);
    path->setPlaceholderText(tr("e.g. kubejs/ or config/options.txt"));
    auto* policy = new QComboBox(&dialog);
    policy->addItems({QStringLiteral("replace"), QStringLiteral("if_missing")});
    form->addRow(tr("Relative path"), path);
    form->addRow(tr("Default policy"), policy);
    layout->addLayout(form);
    auto* client = new QCheckBox(tr("Client"), &dialog);
    auto* server = new QCheckBox(tr("Server"), &dialog);
    client->setChecked(true);
    server->setChecked(true);
    layout->addWidget(client);
    layout->addWidget(server);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || path->text().trimmed().isEmpty() || (!client->isChecked() && !server->isChecked()))
        return;
    QStringList targets;
    if (client->isChecked()) targets.append(QStringLiteral("client"));
    if (server->isChecked()) targets.append(QStringLiteral("server"));
    runOperation("set-tracked-path", {{"path", path->text().trimmed()}, {"targets", toTargets(targets)}, {"policy", policy->currentText()}, {"enabled", true}},
                 [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::removeTrackedPath()
{
    const int row = m_trackedTable->currentIndex().row();
    const auto tracked = m_trackedModel->entryAt(row);
    if (tracked.isEmpty()) return;
    if (QMessageBox::question(this, tr("Stop watching this path"),
                              tr("Stop tracking %1? Existing files on this computer will stay in place.").arg(tracked.value("path").toString()),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    runOperation("set-tracked-path", {{"path", tracked.value("path")}, {"targets", toTargets(targetsFor(tracked))},
                                       {"policy", tracked.value("policy")}, {"enabled", false}},
                 [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::removeSelectedResource()
{
    const auto mod = selectedMod();
    if (mod.isEmpty()) return;
    const auto reply = QMessageBox::warning(this, tr("Remove mod from pack"),
                                            tr("Remove %1 from the pack and delete its matching local file(s)? ModLock checks the file hashes and stops if a file has changed locally.")
                                                .arg(mod.value("name").toString(mod.value("id").toString())),
                                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply != QMessageBox::Yes) return;
    QJsonObject params{{"kind", "mod"}, {"identity", mod.value("identity")}, {"targets", mod.value("targets")}};
    if (!mod.value("sha256").toString().isEmpty()) params.insert("expected_sha256", mod.value("sha256"));
    params.insert("target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot()));
    runOperation("remove-resource", params, [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::showPublishPreview()
{
    m_previewId.clear();
    runOperation("publish-preview", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}});
}

void PackEditorPage::publish(const QString& commitMessage)
{
    if (m_previewId.isEmpty() || commitMessage.trimmed().isEmpty()) return;
    runOperation("publish", {{"preview_id", m_previewId}, {"commit_message", commitMessage.trimmed()},
                              {"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}});
}

void PackEditorPage::showStatusMessage(const QString& message, bool error)
{
    m_status->setText(message);
    if (error)
        QMessageBox::warning(this, tr("Pack Editor"), message);
}
