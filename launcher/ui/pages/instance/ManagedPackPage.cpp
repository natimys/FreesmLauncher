// SPDX-FileCopyrightText: 2022 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ManagedPackPage.h"
#include "modlock/ModLockBridge.h"
#include <QDesktopServices>
#include <QJsonDocument>
#include <QLineEdit>
#include <QUrl>
#include <QUrlQuery>
#include "modplatform/ModIndex.h"
#include "ui_ManagedPackPage.h"

#include <QFileDialog>
#include <QListView>
#include <QMessageBox>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleFactory>
#include <memory>

#include "Application.h"
#include "BuildConfig.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "InstanceTask.h"
#include "Json.h"
#include "Markdown.h"
#include "StringUtils.h"

#include "ui/InstanceWindow.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"

#include "net/ApiDownload.h"

/** This is just to override the combo box popup behavior so that the combo box doesn't take the whole screen.
 *  ... thanks Qt.
 */
class NoBigComboBoxStyle : public QProxyStyle {
    Q_OBJECT

   public:
    // clang-format off
    int styleHint(QStyle::StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override
    {
        if (hint == QStyle::SH_ComboBox_Popup)
            return false;

        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
    // clang-format on

    /**
     * Something about QProxyStyle and QStyle objects means they can't be free'd just
     * because all the widgets using them are gone.
     * They seems to be tied to the QApplicaiton lifecycle.
     * So make singletons tied to the lifetime of the application to clean them up and ensure they aren't
     * being remade over and over again, thus leaking memory.
     */
   public:
    static NoBigComboBoxStyle* getInstance(QStyle* style)
    {
        static QHash<QStyle*, NoBigComboBoxStyle*> s_singleton_instances_ = {};
        static std::mutex s_singleton_instances_mutex_;

        std::lock_guard<std::mutex> lock(s_singleton_instances_mutex_);
        auto inst_iter = s_singleton_instances_.constFind(style);
        NoBigComboBoxStyle* inst = nullptr;
        if (inst_iter == s_singleton_instances_.constEnd() || *inst_iter == nullptr) {
            inst = new NoBigComboBoxStyle(style);
            inst->setParent(APPLICATION);
            s_singleton_instances_.insert(style, inst);
            qDebug() << "QProxyStyle NoBigComboBox created for" << style->objectName() << style;
        } else {
            inst = *inst_iter;
        }
        return inst;
    }

   private:
    NoBigComboBoxStyle(QStyle* style) : QProxyStyle(style) {}
};

ManagedPackPage* ManagedPackPage::createPage(BaseInstance* inst, QString type, QWidget* parent)
{
    if (type == "modrinth")
        return new ModrinthManagedPackPage(inst, nullptr, parent);
    if (type == "flame" && (APPLICATION->capabilities() & Application::SupportsFlame))
        return new FlameManagedPackPage(inst, nullptr, parent);
    if (type == "modlock")
        return new ModLockManagedPackPage(inst, nullptr, parent);

    return new GenericManagedPackPage(inst, nullptr, parent);
}

ManagedPackPage::ManagedPackPage(BaseInstance* inst, InstanceWindow* instance_window, QWidget* parent)
    : QWidget(parent), m_instance_window(instance_window), ui(new Ui::ManagedPackPage), m_inst(inst)
{
    Q_ASSERT(inst);

    ui->setupUi(this);

    // NOTE: GTK2 themes crash with the proxy style.
    // This seems like an upstream bug, so there's not much else that can be done.
    if (!QStyleFactory::keys().contains("gtk2")) {
        auto comboStyle = NoBigComboBoxStyle::getInstance(ui->versionsComboBox->style());
        ui->versionsComboBox->setStyle(comboStyle);
    }

    ui->versionsComboBox->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->versionsComboBox->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    ui->reloadButton->setVisible(false);
    connect(ui->reloadButton, &QPushButton::clicked, this, [this](bool) {
        ui->reloadButton->setVisible(false);

        m_loaded = false;
        // Pretend we're opening the page again
        openedImpl();
    });

    connect(ui->changelogTextBrowser, &QTextBrowser::anchorClicked, this, [](const QUrl url) {
        if (url.scheme().isEmpty()) {
            auto querry =
                QUrlQuery(url.query()).queryItemValue("remoteUrl", QUrl::FullyDecoded);  // curseforge workaround for linkout?remoteUrl=
            auto decoded = QUrl::fromPercentEncoding(querry.toUtf8());
            auto newUrl = QUrl(decoded);
            if (newUrl.isValid() && (newUrl.scheme() == "http" || newUrl.scheme() == "https"))
                QDesktopServices ::openUrl(newUrl);
            return;
        }
        QDesktopServices::openUrl(url);
    });

    connect(ui->urlLine, &QLineEdit::textChanged, this, [this](QString text) { m_inst->settings()->set("ManagedPackURL", text.trimmed()); });
}

ManagedPackPage::~ManagedPackPage()
{
    delete ui;
}

void ManagedPackPage::openedImpl()
{
    if (m_inst->getManagedPackID().isEmpty()) {
        ui->packVersion->hide();
        ui->packVersionLabel->hide();
        ui->packOrigin->hide();
        ui->packOriginLabel->hide();
        ui->versionsComboBox->hide();
        ui->updateToVersionLabel->setText(tr("URL:"));
        ui->updateButton->setText(tr("Update Pack"));
        ui->updateButton->setDisabled(false);
        ui->urlLine->setText(m_inst->settings()->get("ManagedPackURL").toString().trimmed());

        ui->packName->setText(m_inst->name());
        ui->changelogTextBrowser->setText(tr("This is a local modpack.\n"
                                             "This can be updated either using a file in %1 format or an URL.\n"
                                             "Do not use a different format than the one mentioned as it may break the instance.\n"
                                             "Make sure you also trust the URL.\n")
                                              .arg(displayName()));
        return;
    }
    ui->urlLine->hide();
    ui->packName->setText(m_inst->getManagedPackName());
    ui->packVersion->setText(m_inst->getManagedPackVersionName());
    ui->packOrigin->setText(tr("Website: <a href=%1>%2</a>    |    Pack ID: %3    |    Version ID: %4")
                                .arg(url(), displayName(), m_inst->getManagedPackID(), m_inst->getManagedPackVersionID()));

    parseManagedPack();
}

QString ManagedPackPage::displayName() const
{
    auto type = m_inst->getManagedPackType();
    if (type.isEmpty())
        return {};
    if (type == "flame")
        type = "CurseForge";
    return type.replace(0, 1, type[0].toUpper());
}

QIcon ManagedPackPage::icon() const
{
    return QIcon::fromTheme(m_inst->getManagedPackType());
}

QString ManagedPackPage::helpPage() const
{
    return {};
}

void ManagedPackPage::retranslate()
{
    ui->retranslateUi(this);
}

bool ManagedPackPage::shouldDisplay() const
{
    return m_inst->isManagedPack();
}

bool ManagedPackPage::runUpdateTask(InstanceTask* task)
{
    Q_ASSERT(task);

    unique_qobject_ptr<Task> wrapped_task(APPLICATION->instances()->wrapInstanceTask(task));

    connect(wrapped_task.get(), &Task::failed,
            [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
    connect(wrapped_task.get(), &Task::succeeded, [this, task]() {
        QStringList warnings = task->warnings();
        if (warnings.count()) {
            CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
        }
    });
    connect(wrapped_task.get(), &Task::aborted, [this] {
        CustomMessageBox::selectable(this, tr("Task aborted"), tr("The task has been aborted by the user."), QMessageBox::Information)
            ->show();
    });

    ProgressDialog loadDialog(this);
    loadDialog.setSkipButton(true, tr("Abort"));
    loadDialog.execWithTask(wrapped_task.get());

    return wrapped_task->wasSuccessful();
}

void ManagedPackPage::suggestVersion()
{
    ui->updateButton->setText(tr("Update Pack"));
    ui->updateButton->setDisabled(false);
}

void ManagedPackPage::setFailState()
{
    qDebug() << "Setting fail state!";

    // We block signals here so that suggestVersion() doesn't get called, causing an assertion fail.
    ui->versionsComboBox->blockSignals(true);
    ui->versionsComboBox->clear();
    ui->versionsComboBox->addItem(tr("Failed to search for available versions."), {});
    ui->versionsComboBox->blockSignals(false);

    ui->changelogTextBrowser->setText(tr("Failed to request changelog data for this modpack."));

    ui->updateButton->setText(tr("Cannot update!"));
    ui->updateButton->setDisabled(true);

    ui->reloadButton->setVisible(true);
}

ModrinthManagedPackPage::ModrinthManagedPackPage(BaseInstance* inst, InstanceWindow* instance_window, QWidget* parent)
    : ManagedPackPage(inst, instance_window, parent)
{
    Q_ASSERT(inst->isManagedPack());
    connect(ui->versionsComboBox, &QComboBox::currentIndexChanged, this, &ModrinthManagedPackPage::suggestVersion);
    connect(ui->updateButton, &QPushButton::clicked, this, &ModrinthManagedPackPage::update);
    connect(ui->updateFromFileButton, &QPushButton::clicked, this, &ModrinthManagedPackPage::updateFromFile);
}

// MODRINTH
void ModrinthManagedPackPage::parseManagedPack()
{
    qDebug() << "Parsing Modrinth pack";

    // No need for the extra work because we already have everything we need.
    if (m_loaded) {
        return;
    }

    if (m_fetch_job && m_fetch_job->isRunning()) {
        m_fetch_job->abort();
    }

    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks{};
    m_pack = { .addonId = m_inst->getManagedPackID() };

    // Use default if no callbacks are set
    callbacks.on_succeed = [this](auto& doc) {
        m_pack.versions = doc;
        m_pack.versionsLoaded = true;

        // We block signals here so that suggestVersion() doesn't get called, causing an assertion fail.
        ui->versionsComboBox->blockSignals(true);
        ui->versionsComboBox->clear();
        ui->versionsComboBox->blockSignals(false);

        for (const auto& version : m_pack.versions) {
            QString name = version.getVersionDisplayString();

            // NOTE: the id from version isn't the same id in the modpack format spec...
            // e.g. HexMC's 4.4.0 has versionId 4.0.0 in the modpack index..............
            if (version.version == m_inst->getManagedPackVersionName()) {
                name = tr("%1 (Current)").arg(name);
            }

            ui->versionsComboBox->addItem(name, version.fileId);
        }

        suggestVersion();

        m_loaded = true;
    };
    callbacks.on_fail = [this](const QString& /*reason*/, int) { setFailState(); };
    callbacks.on_abort = [this]() { setFailState(); };
    m_fetch_job = m_api.getProjectVersions({ .pack = std::make_shared<ModPlatform::IndexedPack>(m_pack),
                                             .mcVersions = {},
                                             .loaders = {},
                                             .resourceType = ModPlatform::ResourceType::Modpack,
                                             .includeChangelog = true },
                                           std::move(callbacks));

    ui->changelogTextBrowser->setText(tr("Fetching changelogs..."));

    m_fetch_job->start();
}

QString ModrinthManagedPackPage::url() const
{
    return "https://modrinth.com/mod/" + m_inst->getManagedPackID();
}

void ModrinthManagedPackPage::suggestVersion()
{
    auto index = ui->versionsComboBox->currentIndex();
    if (m_pack.versions.length() == 0) {
        setFailState();
        return;
    }
    auto version = m_pack.versions.at(index);

    ui->changelogTextBrowser->setHtml(StringUtils::htmlListPatch(markdownToHTML(version.changelog.toUtf8())));

    ManagedPackPage::suggestVersion();
}

/// @brief Called when the update task has completed.
/// Internally handles the closing of the instance window if the update was successful and shows a message box.
/// @param did_succeed Whether the update task was successful.
void ManagedPackPage::onUpdateTaskCompleted(bool did_succeed) const
{
    // Close the window if the update was successful
    if (did_succeed) {
        if (m_instance_window != nullptr)
            m_instance_window->close();

        CustomMessageBox::selectable(nullptr, tr("Update Successful"),
                                     tr("The instance updated to pack version %1 successfully.").arg(m_inst->getManagedPackVersionName()),
                                     QMessageBox::Information)
            ->show();
    } else {
        CustomMessageBox::selectable(
            nullptr, tr("Update Failed"),
            tr("The instance failed to update to pack version %1. Please check launcher logs for more information.")
                .arg(m_inst->getManagedPackVersionName()),
            QMessageBox::Critical)
            ->show();
    }
}

void ModrinthManagedPackPage::update()
{
    auto customURL = m_inst->settings()->get("ManagedPackURL").toString().trimmed();
    if (m_inst->getManagedPackID().isEmpty() && !customURL.isEmpty()) {
        updatePack(customURL, false);
        return;
    }
    auto index = ui->versionsComboBox->currentIndex();
    if (m_pack.versions.length() == 0) {
        setFailState();
        return;
    }
    auto version = m_pack.versions.at(index);

    updatePack(version.downloadUrl, true, version.fileId.toString(), version.version);
}

void ModrinthManagedPackPage::updateFromFile()
{
    auto output = QFileDialog::getOpenFileUrl(this, tr("Choose update file"), QDir::homePath(), tr("Modrinth pack") + " (*.mrpack *.zip)");
    if (output.isEmpty())
        return;

    updatePack(output, false);
}

// FLAME
FlameManagedPackPage::FlameManagedPackPage(BaseInstance* inst, InstanceWindow* instance_window, QWidget* parent)
    : ManagedPackPage(inst, instance_window, parent)
{
    Q_ASSERT(inst->isManagedPack());
    connect(ui->versionsComboBox, &QComboBox::currentIndexChanged, this, &FlameManagedPackPage::suggestVersion);
    connect(ui->updateButton, &QPushButton::clicked, this, &FlameManagedPackPage::update);
    connect(ui->updateFromFileButton, &QPushButton::clicked, this, &FlameManagedPackPage::updateFromFile);
}

void FlameManagedPackPage::parseManagedPack()
{
    qDebug() << "Parsing Flame pack";

    // We need to tell the user to redownload the pack, since we didn't save the required info previously
    if (m_inst->getManagedPackID().isEmpty()) {
        setFailState();
        QString message =
            tr("<h1>Hey there!</h1>"
               "<h4>"
               "It seems like your Pack ID is null. This is because of a bug in older versions of the launcher.<br/>"
               "Unfortunately, we can't do the proper API requests without this information.<br/>"
               "<br/>"
               "So, in order for this feature to work, you will need to re-download the modpack from the built-in downloader.<br/>"
               "<br/>"
               "Don't worry though, it will ask you to update this instance instead, so you'll not lose this instance!"
               "</h4>");

        ui->changelogTextBrowser->setHtml(StringUtils::htmlListPatch(message));
        return;
    }

    // No need for the extra work because we already have everything we need.
    if (m_loaded) {
        return;
    }

    if (m_fetch_job && m_fetch_job->isRunning()) {
        m_fetch_job->abort();
    }

    QString id = m_inst->getManagedPackID();
    m_pack = { .addonId = id };

    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks{};

    // Use default if no callbacks are set
    callbacks.on_succeed = [this](auto& doc) {
        m_pack.versions = doc;
        m_pack.versionsLoaded = true;

        // We block signals here so that suggestVersion() doesn't get called, causing an assertion fail.
        ui->versionsComboBox->blockSignals(true);
        ui->versionsComboBox->clear();
        ui->versionsComboBox->blockSignals(false);

        for (const auto& version : m_pack.versions) {
            QString name = version.getVersionDisplayString();

            if (version.fileId == m_inst->getManagedPackVersionID().toInt()) {
                name = tr("%1 (Current)").arg(name);
            }

            ui->versionsComboBox->addItem(name, QVariant(version.fileId));
        }

        suggestVersion();

        m_loaded = true;
    };
    callbacks.on_fail = [this](const QString& /*reason*/, int) { setFailState(); };
    callbacks.on_abort = [this]() { setFailState(); };
    m_fetch_job = m_api.getProjectVersions({ .pack = std::make_shared<ModPlatform::IndexedPack>(m_pack),
                                             .mcVersions = {},
                                             .loaders = {},
                                             .resourceType = ModPlatform::ResourceType::Modpack,
                                             .includeChangelog = true },
                                           std::move(callbacks));

    m_fetch_job->start();
}

QString FlameManagedPackPage::url() const
{
    // FIXME: We should display the websiteUrl field, but this requires doing the API request first :(
    return "https://www.curseforge.com/projects/" + m_inst->getManagedPackID();
}

void FlameManagedPackPage::suggestVersion()
{
    auto index = ui->versionsComboBox->currentIndex();
    if (m_pack.versions.length() == 0) {
        setFailState();
        return;
    }
    auto version = m_pack.versions.at(index);

    ui->changelogTextBrowser->setHtml(
        StringUtils::htmlListPatch(m_api.getModFileChangelog(m_inst->getManagedPackID().toInt(), version.fileId.toInt())));

    ManagedPackPage::suggestVersion();
}

void FlameManagedPackPage::update()
{
    auto customURL = m_inst->settings()->get("ManagedPackURL").toString().trimmed();
    if (m_inst->getManagedPackID().isEmpty() && !customURL.isEmpty()) {
        updatePack(customURL, false);
        return;
    }
    auto index = ui->versionsComboBox->currentIndex();
    if (m_pack.versions.length() == 0) {
        setFailState();
        return;
    }
    auto version = m_pack.versions.at(index);

    updatePack(version.downloadUrl, true, version.fileId.toString());
}

void FlameManagedPackPage::updateFromFile()
{
    auto output = QFileDialog::getOpenFileUrl(this, tr("Choose update file"), QDir::homePath(), tr("CurseForge pack") + " (*.zip)");
    if (output.isEmpty())
        return;

    updatePack(output, false);
}

ModLockManagedPackPage::ModLockManagedPackPage(BaseInstance* inst, InstanceWindow* instance_window, QWidget* parent)
    : ManagedPackPage(inst, instance_window, parent)
{
    connect(ui->updateButton, &QPushButton::clicked, this, &ModLockManagedPackPage::update);
    connect(ui->updateFromFileButton, &QPushButton::clicked, this, &ModLockManagedPackPage::update);
    ui->updateFromFileButton->hide();
    ui->versionsComboBox->hide();
    ui->urlLine->hide();
}

void ModLockManagedPackPage::openedImpl()
{
    ManagedPackPage::openedImpl();
    ui->packName->setText(m_inst->getManagedPackName());
    ui->packVersion->setText(m_inst->getManagedPackVersionName());
    ui->packOriginLabel->setText(tr("Repository / branch / lock file:"));
    const QString repository = m_inst->settings()->get("ManagedPackURL").toString();
    const QString branch = m_inst->settings()->get("ModLockBranch").toString();
    const QString lockPath = m_inst->settings()->get("ModLockLockPath").toString();
    ui->packOrigin->setText(QStringLiteral("<a href=\"%1\">%1</a> &nbsp;|&nbsp; %2 &nbsp;|&nbsp; %3")
                                .arg(repository.toHtmlEscaped(), branch.toHtmlEscaped(), lockPath.toHtmlEscaped()));
    ui->updateToVersionLabel->setText(tr("ModLock updates:"));
    ui->updateButton->setText(tr("Check for updates"));
    ui->updateButton->setEnabled(!m_inst->isRunning());
    ui->changelogBox->setTitle(tr("Update preview"));
    ui->changelogTextBrowser->setText(tr("Check the source for changes. The preview is pinned to a Git revision; applying it installs that exact revision."));
}

void ModLockManagedPackPage::setBusy(bool busy, const QString& status)
{
    m_busy = busy;
    ui->updateButton->setEnabled(!busy && !m_inst->isRunning());
    if (!status.isEmpty())
        ui->changelogTextBrowser->setPlainText(status);
}

bool ModLockManagedPackPage::prepareToClose()
{
    if (!m_bridge || !m_bridge->isActive())
        return true;

    QMessageBox box(QMessageBox::Warning, tr("ModLock operation is running"),
                    tr("Wait for the ModLock check or file update to finish, or send a cooperative cancellation. The window will stay open until the process exits."),
                    QMessageBox::NoButton, m_instance_window);
    auto* wait = box.addButton(tr("Wait"), QMessageBox::RejectRole);
    auto* cancel = box.addButton(tr("Cancel operation"), QMessageBox::DestructiveRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(wait));
    box.exec();
    if (box.clickedButton() == cancel) {
        if (m_bridge->cancel())
            setBusy(true, tr("Waiting for ModLock to finish safely..."));
    }
    return false;
}

void ModLockManagedPackPage::update()
{
    if (!m_previewRevision.isEmpty()) {
        applyPreview();
        return;
    }
    if (m_busy || m_inst->isRunning()) {
        CustomMessageBox::selectable(this, tr("ModLock update unavailable"),
                                     tr("Stop the running game before checking or applying a ModLock update."), QMessageBox::Information)
            ->show();
        return;
    }

    m_previewRevision.clear();
    m_operation = "check";
    m_result = {};
    m_error = {};
    setBusy(true, tr("Checking the ModLock source..."));
    m_bridge = new ModLockBridge(m_inst->gameRoot(), this);
    connect(m_bridge, &ModLockBridge::progress, this, [this](const QString&, const QString& message) {
        ui->changelogTextBrowser->setPlainText(message);
    });
    connect(m_bridge, &ModLockBridge::completed, this, [this](const QString&, const QJsonObject& result) {
        m_result = result;
    });
    connect(m_bridge, &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) {
        m_error = error;
    });
    connect(m_bridge, &ModLockBridge::finished, this, &ModLockManagedPackPage::onBridgeFinished);
    if (!m_bridge->start("check")) {
        setBusy(false, tr("Could not start the ModLock component."));
        m_bridge->deleteLater();
        m_bridge = nullptr;
    }
}

void ModLockManagedPackPage::applyPreview()
{
    if (m_previewRevision.isEmpty() || m_busy || m_inst->isRunning())
        return;
    m_busy = true;
    m_operation = "apply";
    m_result = {};
    m_error = {};
    ui->updateButton->setEnabled(false);
    ui->changelogTextBrowser->setPlainText(tr("Applying the previewed revision %1...").arg(m_previewRevision));
    m_bridge = new ModLockBridge(m_inst->gameRoot(), this);
    connect(m_bridge, &ModLockBridge::progress, this, [this](const QString&, const QString& message) {
        ui->changelogTextBrowser->setPlainText(message);
    });
    connect(m_bridge, &ModLockBridge::completed, this, [this](const QString&, const QJsonObject& result) {
        m_result = result;
    });
    connect(m_bridge, &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) {
        m_error = error;
    });
    connect(m_bridge, &ModLockBridge::finished, this, &ModLockManagedPackPage::onBridgeFinished);
    if (!m_bridge->start("apply", {{"revision", m_previewRevision}})) {
        setBusy(false, tr("Could not start the ModLock component."));
        m_bridge->deleteLater();
        m_bridge = nullptr;
    }
}

void ModLockManagedPackPage::onBridgeFinished()
{
    if (m_bridge) {
        m_bridge->deleteLater();
        m_bridge = nullptr;
    }
    if (!m_error.isEmpty()) {
        const QString code = m_error.value("code").toString();
        const QString message = m_error.value("message").toString(tr("ModLock could not complete the request."));
        if (code == "cancelled") {
            setBusy(false, tr("ModLock operation cancelled."));
        } else {
            setBusy(false, tr("ModLock error: %1").arg(message).toHtmlEscaped());
            const bool recovery = code == "recovery_failed";
            CustomMessageBox::selectable(this, recovery ? tr("ModLock recovery failed") : tr("ModLock update error"),
                                         message, QMessageBox::Critical)->show();
        }
        return;
    }

    if (m_operation == "check") {
        m_previewRevision = m_result.value("revision").toString();
        const QJsonObject local = m_result.value("local_installation").toObject();
        const bool needsRecovery = local.value("needs_recovery").toBool();
        const QString diffText = QString::fromUtf8(QJsonDocument(m_result.value("diff").toObject()).toJson(QJsonDocument::Indented));
        if (!m_previewRevision.isEmpty() && m_previewRevision == m_inst->getManagedPackVersionID() && !needsRecovery) {
            ui->changelogTextBrowser->setPlainText(tr("This instance is at the checked revision and its managed files are present: %1")
                                                       .arg(m_previewRevision));
            ui->updateButton->setText(tr("Check for updates"));
            setBusy(false);
            m_previewRevision.clear();
        } else {
            const QString state = needsRecovery ? tr("\nManaged files are missing or damaged and will be restored.") : QString();
            ui->changelogTextBrowser->setPlainText(tr("Preview revision: %1%2\n\n%3").arg(m_previewRevision, state, diffText));
            ui->updateButton->setText(needsRecovery ? tr("Restore / apply preview") : tr("Apply previewed update"));
            setBusy(false);
            ui->updateButton->setEnabled(!m_previewRevision.isEmpty() && !m_inst->isRunning());
        }
        return;
    }

    const QString revision = m_result.value("revision").toString();
    if (!revision.isEmpty()) {
        m_inst->settings()->set("ManagedPackVersionID", revision);
        m_inst->settings()->set("ManagedPackVersionName", revision.left(12));
        ui->packVersion->setText(revision.left(12));
    }
    m_previewRevision.clear();
    ui->updateButton->setText(tr("Check for updates"));
    setBusy(false, tr("ModLock update applied successfully. Installed revision: %1").arg(revision).toHtmlEscaped());
}

void ManagedPackPage::updatePack(const QUrl& url, bool trusted, QString versionID, QString versionName)
{
    QMap<QString, QString> extra_info;
    // NOTE: Don't use 'm_pack.id' here, since we didn't completely parse all the metadata for the pack, including this field.
    extra_info.insert("pack_id", m_inst->getManagedPackID());
    extra_info.insert("pack_version_id", versionID);
    extra_info.insert("original_instance_id", m_inst->id());

    auto extracted = new InstanceImportTask(url, trusted, this, std::move(extra_info));

    if (versionName.isEmpty()) {
        extracted->setName(m_inst->name());
    } else {
        InstanceName inst_name(m_inst->getManagedPackName(), versionName);
        inst_name.setName(m_inst->name().replace(m_inst->getManagedPackVersionName(), versionName));
        extracted->setName(inst_name);
    }
    extracted->setGroup(APPLICATION->instances()->getInstanceGroup(m_inst->id()));
    extracted->setIcon(m_inst->iconKey());
    extracted->setConfirmUpdate(false);

    // Run our task then handle the result
    auto did_succeed = runUpdateTask(extracted);
    onUpdateTaskCompleted(did_succeed);
}

#include "ManagedPackPage.moc"
