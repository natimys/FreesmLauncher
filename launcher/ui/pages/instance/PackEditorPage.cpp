// SPDX-License-Identifier: GPL-3.0-only
#include "PackEditorPage.h"

#include <QCheckBox>
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
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "BaseInstance.h"
#include "Application.h"
#include "modlock/ModLockAddModTask.h"
#include "modlock/ModLockBridge.h"
#include "minecraft/MinecraftInstance.h"
#include "modplatform/ModIndex.h"
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

void setTableHeaders(QTableWidget* table, const QStringList& headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

void appendTableRow(QTableWidget* table, const QStringList& values)
{
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < values.size(); ++column)
        table->setItem(row, column, new QTableWidgetItem(values.at(column)));
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
    clientLayout->addWidget(m_clientView);
    auto* serverBox = new QGroupBox(tr("Server"), modsTab);
    auto* serverLayout = new QVBoxLayout(serverBox);
    m_serverView = new QListView(serverBox);
    m_serverView->setModel(&m_serverModel);
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
    m_filesTable = new QTableWidget(filesTab);
    setTableHeaders(m_filesTable, {tr("Path"), tr("Destination"), tr("Targets"), tr("Policy"), tr("SHA-256"), tr("Status")});
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
    m_trackedTable = new QTableWidget(trackedTab);
    setTableHeaders(m_trackedTable, {tr("Path"), tr("Targets"), tr("Policy"), tr("State")});
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
    m_ignoredTable = new QTableWidget(ignoredTab);
    setTableHeaders(m_ignoredTable, {tr("Resource"), tr("Kind"), tr("Targets"), tr("Status")});
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
    connect(m_scanButton, &QPushButton::clicked, this, [this] { runOperation("author-scan", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}}); });
    connect(m_previewButton, &QPushButton::clicked, this, &PackEditorPage::showPublishPreview);
    connect(m_publishButton, &QPushButton::clicked, this, [this] { publish(); });
    connect(m_showShared, &QCheckBox::toggled, this, [this](bool value) {
        m_clientModel.setShowShared(value);
        m_serverModel.setShowShared(value);
    });
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
    connect(m_filesTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const bool selected = m_filesTable->currentRow() >= 0;
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
    return m_instance && m_minecraftInstance &&
           packEditorIsAuthorMode(m_instance->getManagedPackType(),
                                  QFileInfo::exists(QDir(m_minecraftInstance->gameRoot()).filePath(QStringLiteral(".modlock/author.toml"))));
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
    runOperation("author-state", {{"target_roots", modLockTargetRoots(m_minecraftInstance->gameRoot(), m_minecraftInstance->instanceRoot())}});
}

void PackEditorPage::refreshView()
{
    m_clientModel.setEntries(m_state.mods);
    m_serverModel.setEntries(m_state.mods);
    m_filesTable->setRowCount(0);
    for (const auto& item : m_state.files) {
        const auto file = item.toObject();
        appendTableRow(m_filesTable, {file.value("path").toString(), file.value("target").toString(), displayTargets(file),
                                      file.value("policy").toString(), file.value("sha256").toString(), packEditorStatusSummary(file)});
    }
    m_trackedTable->setRowCount(0);
    for (const auto& item : m_state.trackedPaths) {
        const auto path = item.toObject();
        appendTableRow(m_trackedTable, {path.value("path").toString(), displayTargets(path), path.value("policy").toString(),
                                        path.value("enabled").toBool() ? tr("Tracked") : tr("Disabled")});
    }
    m_ignoredTable->setRowCount(0);
    for (const auto& item : m_state.mods) {
        const auto mod = item.toObject();
        if (mod.value("status").toString() == QStringLiteral("ignored"))
            appendTableRow(m_ignoredTable, {mod.value("name").toString(mod.value("id").toString()), tr("Mod"), displayTargets(mod), tr("Ignored")});
    }
    for (const auto& item : m_state.files) {
        const auto file = item.toObject();
        if (file.value("status").toString() == QStringLiteral("ignored"))
            appendTableRow(m_ignoredTable, {file.value("path").toString(), tr("File"), displayTargets(file), tr("Ignored")});
    }
    m_status->setText(tr("Branch: %1%2").arg(m_state.branch, m_state.dirty ? tr(" — local changes") : QString()));
}

void PackEditorPage::setBusy(bool busy)
{
    for (auto* button : {m_refreshButton, m_scanButton, m_previewButton, m_publishButton, m_addModButton,
                         m_clientOnlyButton, m_serverOnlyButton, m_sharedButton, m_ignoreButton, m_unmanageButton,
                         m_removeResourceButton, m_addTrackedButton, m_removeTrackedButton, m_updateModButton,
                         m_ignoreFileButton, m_unmanageFileButton, m_removeFileButton})
        button->setEnabled(!busy);
    if (!busy && !m_previewId.isEmpty())
        m_publishButton->setEnabled(true);
}

void PackEditorPage::runOperation(const QString& operation,
                                  const QJsonObject& params,
                                  std::function<void(const QJsonObject&)> onSuccess)
{
    if (m_bridge || !m_minecraftInstance)
        return;
    m_operation = operation;
    m_result = {};
    m_error = {};
    m_onSuccess = std::move(onSuccess);
    setBusy(true);
    m_bridge = new ModLockBridge(m_minecraftInstance->gameRoot(), this);
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
    } else if (m_operation == QStringLiteral("author-scan")) {
        QString error;
        if (!PackEditorAuthorState::parse(m_result, &m_state, &error)) {
            showStatusMessage(error, true);
            return;
        }
        refreshView();
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
    runOperation("set-mod-targets", {{"id", mod.value("id")}, {"targets", targets}}, [this](const QJsonObject&) { loadAuthorState(); });
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
    const QStringList defaultTargets = existingMod.isEmpty() ? QStringList{QStringLiteral("client")} : targetsFor(existingMod);
    connect(dialog, &QDialog::finished, this, [this, dialog, defaultTargets](int result) {
        QList<ResourceDownload::ResourceDownloadDialog::DownloadTaskPtr> selected;
        if (result == QDialog::Accepted)
            selected = dialog->getTasks();
        dialog->deleteLater();
        for (const auto& task : selected) {
            TargetChoiceDialog targets(this, defaultTargets);
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
                               {"version", version.version},
                               {"filename", version.fileName},
                               {"source", source},
                               {"provider", source},
                               {"project_id", projectId},
                               {"version_id", version.fileId.toString()},
                               {"mod_id", projectId},
                               {"file_id", version.fileId.toString()},
                               {"url", version.downloadUrl}};
    auto* task = new ModLockAddModTask(m_minecraftInstance->gameRoot(), metadata, toTargets(targets), this);
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
    const int row = m_filesTable->currentRow();
    if (row < 0 || row >= m_state.files.size()) return;
    const auto file = m_state.files.at(row).toObject();
    runOperation("set-resource-state", {{"kind", "file"}, {"identity", file.value("path")}, {"state", state}},
                 [this](const QJsonObject&) { loadAuthorState(); });
}

void PackEditorPage::removeSelectedFile()
{
    const int row = m_filesTable->currentRow();
    if (row < 0 || row >= m_state.files.size()) return;
    const auto file = m_state.files.at(row).toObject();
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
    const int row = m_trackedTable->currentRow();
    if (row < 0 || row >= m_state.trackedPaths.size()) return;
    const auto path = m_state.trackedPaths.at(row).toObject().value("path").toString();
    runOperation("set-tracked-path", {{"path", path}, {"targets", toTargets(targetsFor(m_state.trackedPaths.at(row).toObject()))},
                                       {"policy", m_state.trackedPaths.at(row).toObject().value("policy")}, {"enabled", false}},
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
