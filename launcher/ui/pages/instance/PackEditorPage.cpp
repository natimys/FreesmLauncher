// SPDX-License-Identifier: GPL-3.0-only
#include "PackEditorPage.h"

#include <QCheckBox>
#include <QAbstractTableModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>

#include "BaseInstance.h"
#include "Application.h"
#include "modlock/ModLockAddModTask.h"
#include "modlock/ModLockBridge.h"
#include "minecraft/MinecraftInstance.h"
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
    const auto targets = targetsFor(entry);
    return targets.isEmpty() ? QObject::tr("client (legacy)") : targets.join(QStringLiteral(", "));
}

QJsonArray toTargets(const QStringList& targets)
{
    QJsonArray values;
    for (const auto& target : targets)
        values.append(target);
    return values;
}

QStringList stringArray(const QJsonValue& value)
{
    QStringList result;
    for (const auto& item : value.toArray())
        result.append(item.toString());
    return result;
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
                    case 0: return entry.value("path").toString();
                    case 1: return entry.value("target").toString();
                    case 2: return displayTargets(entry);
                    case 3: return entry.value("policy").toString();
                    case 4: return entry.value("sha256").toString();
                    case 5: return packEditorStatusSummary(entry);
                }
                break;
            case Kind::Tracked:
                switch (index.column()) {
                    case 0: return entry.value("path").toString();
                    case 1: return displayTargets(entry);
                    case 2: return entry.value("policy").toString();
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
    auto* outer = new QVBoxLayout(this);
    auto* header = new QHBoxLayout;
    header->addWidget(new QLabel(tr("Author workspace"), this));
    header->addStretch();
    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    header->addWidget(m_status, 1);
    m_refreshButton = new QPushButton(tr("Refresh"), this);
    m_scanButton = new QPushButton(tr("Scan changes"), this);
    m_previewButton = new QPushButton(tr("Preview publish"), this);
    header->addWidget(m_refreshButton);
    m_promoteLockButton = new QPushButton(tr("Move schema 3 lock to instance root"), this);
    m_promoteLockButton->setVisible(false);
    header->addWidget(m_promoteLockButton);
    header->addWidget(m_scanButton);
    header->addWidget(m_previewButton);
    outer->addLayout(header);

    auto* tabs = new QTabWidget(this);
    auto* modsTab = new QWidget(tabs);
    auto* modsLayout = new QVBoxLayout(modsTab);
    auto* sharedRow = new QHBoxLayout;
    m_showShared = new QCheckBox(tr("Show shared mods"), modsTab);
    m_showShared->setChecked(true);
    sharedRow->addWidget(m_showShared);
    m_addModButton = new QPushButton(tr("Add mod"), modsTab);
    m_updateModButton = new QPushButton(tr("Update"), modsTab);
    sharedRow->addWidget(m_updateModButton);
    sharedRow->addStretch();
    sharedRow->addWidget(m_addModButton);
    modsLayout->addLayout(sharedRow);

    auto* columns = new QHBoxLayout;
    auto* clientBox = new QGroupBox(tr("Client"), modsTab);
    auto* clientLayout = new QVBoxLayout(clientBox);
    m_clientView = new QListView(clientBox);
    m_clientView->setModel(&m_clientModel);
    m_clientView->setIconSize(QSize(32, 32));
    m_clientView->setUniformItemSizes(true);
    clientLayout->addWidget(m_clientView);
    auto* serverBox = new QGroupBox(tr("Server"), modsTab);
    auto* serverLayout = new QVBoxLayout(serverBox);
    m_serverView = new QListView(serverBox);
    m_serverView->setModel(&m_serverModel);
    m_serverView->setIconSize(QSize(32, 32));
    m_serverView->setUniformItemSizes(true);
    serverLayout->addWidget(m_serverView);
    columns->addWidget(clientBox, 1);
    columns->addWidget(serverBox, 1);
    modsLayout->addLayout(columns, 1);

    m_selectedDetails = new QLabel(modsTab);
    m_selectedDetails->setWordWrap(true);
    m_selectedDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    modsLayout->addWidget(m_selectedDetails);
    auto* actions = new QHBoxLayout;
    m_clientOnlyButton = new QPushButton(tr("Client only"), modsTab);
    m_serverOnlyButton = new QPushButton(tr("Server only"), modsTab);
    m_sharedButton = new QPushButton(tr("Shared"), modsTab);
    m_ignoreButton = new QPushButton(tr("Ignore"), modsTab);
    m_unmanageButton = new QPushButton(tr("Stop managing"), modsTab);
    m_removeResourceButton = new QPushButton(tr("Remove"), modsTab);
    for (auto* button : {m_clientOnlyButton, m_serverOnlyButton, m_sharedButton, m_ignoreButton, m_unmanageButton, m_removeResourceButton})
        actions->addWidget(button);
    modsLayout->addLayout(actions);
    tabs->addTab(modsTab, tr("Mods"));

    auto* filesTab = new QWidget(tabs);
    auto* filesLayout = new QVBoxLayout(filesTab);
    m_filesTable = new QTableView(filesTab);
    m_filesModel = new PackEditorTableModel(PackEditorTableModel::Kind::Files,
                                             {tr("Path"), tr("Destination"), tr("Targets"), tr("Policy"), tr("SHA-256"), tr("Status")}, m_filesTable);
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
    tabs->addTab(trackedTab, tr("Tracked Paths"));

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
    tabs->addTab(ignoredTab, tr("Ignored"));
    outer->addWidget(tabs, 1);

    auto* publishRow = new QHBoxLayout;
    publishRow->addWidget(new QLabel(tr("Commit message:"), this));
    m_commitMessage = new QLineEdit(tr("Update modpack"), this);
    publishRow->addWidget(m_commitMessage, 1);
    m_publishButton = new QPushButton(tr("Publish"), this);
    m_publishButton->setEnabled(false);
    publishRow->addWidget(m_publishButton);
    outer->addLayout(publishRow);

    connect(m_refreshButton, &QPushButton::clicked, this, &PackEditorPage::loadAuthorState);
    connect(m_promoteLockButton, &QPushButton::clicked, this, [this] {
        runOperation("promote-schema3-lock", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}},
                     [this](const QJsonObject& result) {
                         showStatusMessage(tr("Moved ModLock workspace to the instance root. Previous lock backup: %1").arg(result.value("backup").toString()));
                         loadAuthorState();
                     });
    });
    connect(m_scanButton, &QPushButton::clicked, this, [this] { runOperation("author-scan", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}}); });
    connect(m_previewButton, &QPushButton::clicked, this, &PackEditorPage::showPublishPreview);
    connect(m_publishButton, &QPushButton::clicked, this, [this] { publish(); });
    connect(m_showShared, &QCheckBox::toggled, this, [this](bool value) {
        m_clientModel.setShowShared(value);
        m_serverModel.setShowShared(value);
    });
    connect(m_clientView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { requestVisibleIcons(); });
    connect(m_serverView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { requestVisibleIcons(); });
    connect(m_addModButton, &QPushButton::clicked, this, [this] { addMods(); });
    connect(m_updateModButton, &QPushButton::clicked, this, [this] { addMods(selectedMod()); });
    connect(m_clientOnlyButton, &QPushButton::clicked, this, [this] { setSelectedTargets(QJsonArray{"client"}); });
    connect(m_serverOnlyButton, &QPushButton::clicked, this, [this] { setSelectedTargets(QJsonArray{"server"}); });
    connect(m_sharedButton, &QPushButton::clicked, this, [this] { setSelectedTargets(QJsonArray{"client", "server"}); });
    connect(m_ignoreButton, &QPushButton::clicked, this, [this] { setSelectedResourceState("ignored"); });
    connect(m_unmanageButton, &QPushButton::clicked, this, [this] { setSelectedResourceState("unmanaged"); });
    connect(m_removeResourceButton, &QPushButton::clicked, this, &PackEditorPage::removeSelectedResource);
    connect(m_ignoreFileButton, &QPushButton::clicked, this, [this] { setSelectedFileState("ignored"); });
    connect(m_unmanageFileButton, &QPushButton::clicked, this, [this] { setSelectedFileState("unmanaged"); });
    connect(m_removeFileButton, &QPushButton::clicked, this, &PackEditorPage::removeSelectedFile);
    connect(m_addTrackedButton, &QPushButton::clicked, this, &PackEditorPage::addTrackedPath);
    connect(m_removeTrackedButton, &QPushButton::clicked, this, &PackEditorPage::removeTrackedPath);
    connect(m_clientView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) {
                    m_serverView->selectionModel()->clear();
                    m_serverView->selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
                }
                showSelectedMod(selectedMod());
            });
    connect(m_serverView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) {
                    m_clientView->selectionModel()->clear();
                    m_clientView->selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
                }
                showSelectedMod(selectedMod());
            });
    connect(m_filesTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        const bool selected = m_filesTable->currentIndex().isValid();
        m_ignoreFileButton->setEnabled(selected);
        m_unmanageFileButton->setEnabled(selected);
        m_removeFileButton->setEnabled(selected);
    });
    m_ignoreFileButton->setEnabled(false);
    m_unmanageFileButton->setEnabled(false);
    m_removeFileButton->setEnabled(false);
}

QIcon PackEditorPage::icon() const { return QIcon::fromTheme(QStringLiteral("modlock")); }

bool PackEditorPage::shouldDisplay() const
{
    return packEditorPageShouldDisplay(m_minecraftInstance != nullptr);
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

void PackEditorPage::openedImpl() { loadAuthorState(); }

void PackEditorPage::loadAuthorState()
{
    if (!shouldDisplay() || m_bridge)
        return;
    const auto lockPath = QDir(m_minecraftInstance->instanceRoot()).filePath(QStringLiteral("mod.lock"));
    m_hasWorkspace = QFileInfo::exists(lockPath);
    m_promoteLockButton->setVisible(!m_hasWorkspace && QFileInfo::exists(QDir(m_minecraftInstance->gameRoot()).filePath(QStringLiteral("mod.lock"))));
    if (!m_hasWorkspace) {
        m_state = {};
        m_clientModel.setEntries({});
        m_serverModel.setEntries({});
        m_filesModel->setEntries({});
        m_trackedModel->setEntries({});
        m_ignoredModel->setEntries({});
        for (auto* button : {m_scanButton, m_previewButton, m_publishButton, m_addModButton, m_updateModButton,
                             m_clientOnlyButton, m_serverOnlyButton, m_sharedButton, m_ignoreButton, m_unmanageButton,
                             m_removeResourceButton, m_addTrackedButton, m_removeTrackedButton, m_ignoreFileButton,
                             m_unmanageFileButton, m_removeFileButton})
            button->setEnabled(false);
        m_status->setText(tr("No ModLock workspace at the instance root. Import a schema 3 pack to manage client and server together."));
        return;
    }
    for (auto* button : {m_scanButton, m_previewButton, m_publishButton, m_addModButton, m_updateModButton,
                         m_clientOnlyButton, m_serverOnlyButton, m_sharedButton, m_ignoreButton, m_unmanageButton,
                         m_removeResourceButton, m_addTrackedButton, m_removeTrackedButton, m_ignoreFileButton,
                         m_unmanageFileButton, m_removeFileButton})
        button->setEnabled(true);
    runOperation("author-state", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}});
}

void PackEditorPage::refreshView()
{
    m_clientModel.setEntries(m_state.mods);
    m_serverModel.setEntries(m_state.mods);
    m_filesModel->setEntries(m_state.files);
    m_trackedModel->setEntries(m_state.trackedPaths);
    QJsonArray ignored;
    for (const auto& item : m_state.mods) ignored.append(item);
    for (const auto& item : m_state.files) ignored.append(item);
    m_ignoredModel->setEntries(ignored);
    m_status->setText(tr("Branch: %1%2").arg(m_state.branch, m_state.dirty ? tr(" — local changes") : QString()));
    m_publishButton->setEnabled(false);
    QTimer::singleShot(0, this, [this] { requestVisibleIcons(); });
}

void PackEditorPage::requestVisibleIcons()
{
    const auto requestFromView = [this](QListView* view, PackEditorTargetModel* model) {
        if (!view || !view->viewport())
            return;
        constexpr int step = 44;
        for (int y = 0; y < view->viewport()->height(); y += step) {
            const auto index = view->indexAt(QPoint(2, y));
            if (index.isValid())
                requestModIcon(model->entryAt(index.row()));
        }
    };
    requestFromView(m_clientView, &m_clientModel);
    requestFromView(m_serverView, &m_serverModel);
}

void PackEditorPage::requestModIcon(const QJsonObject& mod)
{
    const QString identity = mod.value("identity").toString(mod.value("id").toString());
    const QString url = mod.value("icon_url").toString();
    if (identity.isEmpty())
        return;
    if (url.isEmpty()) return;
    if (m_iconCache.contains(url)) {
        m_clientModel.setIcon(identity, m_iconCache.value(url));
        m_serverModel.setIcon(identity, m_iconCache.value(url));
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
                        m_clientModel.setIcon(key, icon);
                        m_serverModel.setIcon(key, icon);
                    }
                }
            }
        }
        reply->deleteLater();
    });
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
    for (auto* button : {m_scanButton, m_previewButton, m_publishButton, m_addModButton,
                         m_clientOnlyButton, m_serverOnlyButton, m_sharedButton, m_ignoreButton, m_unmanageButton,
                         m_removeResourceButton, m_addTrackedButton, m_removeTrackedButton, m_updateModButton,
                         m_ignoreFileButton, m_unmanageFileButton, m_removeFileButton})
        button->setEnabled(!busy && m_hasWorkspace);
    if (!busy && !m_previewId.isEmpty())
        m_publishButton->setEnabled(true);
}

void PackEditorPage::runOperation(const QString& operation,
                                  const QJsonObject& params,
                                  std::function<void(const QJsonObject&)> onSuccess)
{
    if (m_bridge || !m_minecraftInstance)
        return;
    if (operation != QStringLiteral("publish-preview") && operation != QStringLiteral("publish")) {
        m_previewId.clear();
        m_publishButton->setEnabled(false);
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
            m_publishButton->setEnabled(false);
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
            m_publishButton->setEnabled(false);
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
        m_publishButton->setEnabled(!m_previewId.isEmpty());
        showStatusMessage(tr("Preview ready for branch %1. Review the preview details before publishing.").arg(m_result.value("branch").toString()));
        QStringList details;
        details << tr("Branch: %1").arg(m_result.value("branch").toString());
        const auto appendChanges = [&details](const QString& sectionName, const QJsonObject& changes) {
            for (const auto& kind : {QStringLiteral("added"), QStringLiteral("updated"), QStringLiteral("removed")}) {
                QStringList names;
                for (const auto& item : changes.value(kind).toArray()) {
                    const auto entry = item.toObject();
                    QString name = entry.value("name").toString();
                    if (name.isEmpty()) name = entry.value("id").toString();
                    if (name.isEmpty()) name = entry.value("target").toString();
                    if (name.isEmpty()) name = entry.value("path").toString();
                    if (name.isEmpty()) name = item.toString();
                    if (!name.isEmpty()) names.append(name);
                }
                if (!names.isEmpty())
                    details << QObject::tr("%1 %2: %3").arg(sectionName, kind, names.join(QStringLiteral(", ")));
            }
        };
        for (const auto& section : {QStringLiteral("mods"), QStringLiteral("files")}) {
            const auto changes = m_result.value(section).toObject();
            appendChanges(section, changes);
        }
        for (const auto& changeValue : m_result.value("target_changes").toArray()) {
            const auto change = changeValue.toObject();
            QString identity = change.value("id").toString();
            if (identity.isEmpty()) identity = change.value("path").toString();
            details << tr("Target change: %1 (%2 → %3)")
                           .arg(identity, stringArray(change.value("old_targets")).join(", "), stringArray(change.value("targets")).join(", "));
        }
        for (const auto& ignored : m_result.value("ignored").toArray()) {
            const auto entry = ignored.toObject();
            QString identity = entry.value("id").toString();
            if (identity.isEmpty()) identity = entry.value("path").toString();
            if (identity.isEmpty()) identity = ignored.toString();
            details << tr("Ignored: %1").arg(identity);
        }
        details << tr("Commit message: %1").arg(m_result.value("commit_message").toString());
        QMessageBox::information(this, tr("Publish preview"), details.join('\n'));
    } else if (m_operation == QStringLiteral("publish")) {
        m_previewId.clear();
        m_publishButton->setEnabled(false);
        showStatusMessage(tr("Published revision %1 on %2.").arg(m_result.value("revision").toString(), m_result.value("branch").toString()));
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
    const auto clientIndex = m_clientView->currentIndex();
    if (clientIndex.isValid())
        return m_clientModel.entryAt(clientIndex.row());
    const auto serverIndex = m_serverView->currentIndex();
    if (serverIndex.isValid())
        return m_serverModel.entryAt(serverIndex.row());
    return {};
}

void PackEditorPage::showSelectedMod(const QJsonObject& mod)
{
    if (mod.isEmpty()) {
        m_selectedDetails->clear();
        return;
    }
    m_selectedDetails->setText(tr("%1 — %2\nProject/version: %3 / %4\nFilename: %5\nSHA-256: %6\nManaged: %7\nTargets: %8\nSync state: %9")
                                   .arg(mod.value("name").toString(mod.value("id").toString()), mod.value("provider").toString(),
                                        mod.value("project_id").toString(), mod.value("version").toString(),
                                        mod.value("filename").toString(), mod.value("sha256").toString(),
                                        mod.value("managed").toBool() ? tr("yes") : tr("no"), displayTargets(mod),
                                        packEditorStatusSummary(mod)));
}

void PackEditorPage::setSelectedTargets(const QJsonArray& targets)
{
    const auto mod = selectedMod();
    if (mod.isEmpty()) return;
    runOperation("set-mod-targets", packEditorSetModTargetsParams(mod.value("id").toString(), targets),
                 [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::setSelectedResourceState(const QString& state)
{
    const auto mod = selectedMod();
    if (mod.isEmpty()) return;
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
    runOperation("set-resource-state", {{"kind", "file"}, {"identity", file.value("path")}, {"state", state}},
                 [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::removeSelectedFile()
{
    const int row = m_filesTable->currentIndex().row();
    const auto file = m_filesModel->entryAt(row);
    if (file.isEmpty()) return;
    const auto reply = QMessageBox::warning(this, tr("Remove managed file"),
                                            tr("Remove %1 from the desired pack state? ModLock will preserve local bytes if they have changed.")
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
    runOperation("set-tracked-path", {{"path", tracked.value("path")}, {"targets", toTargets(targetsFor(tracked))},
                                       {"policy", tracked.value("policy")}, {"enabled", false}},
                 [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::removeSelectedResource()
{
    const auto mod = selectedMod();
    if (mod.isEmpty()) return;
    const auto reply = QMessageBox::warning(this, tr("Remove mod from pack"),
                                            tr("Remove %1 from the desired pack state? ModLock will recheck its managed files and preserve local bytes if they have changed.")
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
    m_publishButton->setEnabled(false);
    runOperation("publish-preview", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}});
}

void PackEditorPage::publish()
{
    if (m_previewId.isEmpty() || m_commitMessage->text().trimmed().isEmpty()) return;
    runOperation("publish", {{"preview_id", m_previewId}, {"commit_message", m_commitMessage->text().trimmed()},
                              {"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}});
}

void PackEditorPage::showStatusMessage(const QString& message, bool error)
{
    m_status->setText(message);
    if (error)
        QMessageBox::warning(this, tr("Pack Editor"), message);
}
