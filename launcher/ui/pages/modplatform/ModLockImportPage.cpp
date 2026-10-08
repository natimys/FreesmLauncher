// SPDX-License-Identifier: GPL-3.0-only
#include "ModLockImportPage.h"

#include "modlock/ModLockBridge.h"
#include "modlock/ModLockCreationTask.h"
#include "modlock/PackEditorModel.h"
#include "Application.h"
#include "Version.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "tasks/SequentialTask.h"
#include "ui/dialogs/NewInstanceDialog.h"

#include <QDir>
#include <QGroupBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QMessageBox>

ModLockImportPage::ModLockImportPage(NewInstanceDialog* dialog, QWidget* parent)
    : QWidget(parent), m_dialog(dialog), m_repository(new QLineEdit(this)), m_branch(new QLineEdit("main", this)),
      m_lockPath(new QLineEdit("mod.lock", this)), m_advanced(new QGroupBox(tr("Advanced settings"), this)),
      m_status(new QLabel(this)), m_preview(new QPushButton(tr("Preview build"), this))
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_repository->setPlaceholderText(tr("https://github.com/owner/repository"));
    form->addRow(tr("Git repository"), m_repository);
    auto* advancedForm = new QFormLayout(m_advanced);
    advancedForm->addRow(tr("Branch"), m_branch);
    advancedForm->addRow(tr("Lock file path"), m_lockPath);
    layout->addLayout(form);
    layout->addWidget(m_advanced);
    layout->addWidget(m_preview);
    layout->addWidget(m_status);
    layout->addStretch();
    connect(m_preview, &QPushButton::clicked, this, &ModLockImportPage::preview);
    connect(m_repository, &QLineEdit::textChanged, this, &ModLockImportPage::invalidatePreview);
    connect(m_branch, &QLineEdit::textChanged, this, &ModLockImportPage::invalidatePreview);
    connect(m_lockPath, &QLineEdit::textChanged, this, &ModLockImportPage::invalidatePreview);
}

void ModLockImportPage::setRepository(const QString& repository)
{
    m_repository->setText(repository);
    preview();
}

void ModLockImportPage::invalidatePreview()
{
    ++m_generation;
    m_dialog->setModLockSource({}, {}, {}, {});
    m_dialog->setSuggestedPack();
    m_status->clear();
    m_preview->setEnabled(true);
    if (m_bridge && m_bridge->isActive())
        m_bridge->cancel();
}

void ModLockImportPage::preview()
{
    const quint64 generation = ++m_generation;
    m_dialog->setModLockSource({}, {}, {}, {});
    m_dialog->setSuggestedPack();
    const QString repository = m_repository->text().trimmed();
    if (repository.isEmpty() || m_branch->text().trimmed().isEmpty() || m_lockPath->text().trimmed().isEmpty()) {
        m_status->setText(tr("Enter the repository, branch, and lock file path."));
        return;
    }

    const QString root = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/FreesmModLockPreview");
    if (!QDir().mkpath(root)) {
        m_status->setText(tr("Could not prepare the preview directory."));
        return;
    }
    m_bridge = new ModLockBridge(root, this);
    auto* bridge = m_bridge;
    m_preview->setEnabled(false);
    m_status->setText(tr("Reading the build from Git…"));
    connect(bridge, &ModLockBridge::finished, this, [this, bridge] {
        if (m_bridge == bridge)
            m_bridge = nullptr;
        bridge->deleteLater();
    });
    connect(bridge, &ModLockBridge::progress, this, [this, generation](const QString&, const QString& message) {
        if (generation == m_generation)
            m_status->setText(message);
    });
    connect(bridge, &ModLockBridge::completed, this, [this, generation](const QString&, const QJsonObject& result) {
        if (generation != m_generation)
            return;
        const QJsonObject lock = result.value("lock").toObject();
        QJsonObject pack = lock.value("pack").toObject();
        if (lock.isEmpty() || pack.isEmpty()) {
            m_status->setText(tr("The repository did not return a valid ModLock manifest."));
            m_preview->setEnabled(true);
            return;
        }
        const int schema = lock.value("schema").toInt(1);
        if (!modLockImportSchemaSupported(schema)) {
            m_status->setText(tr("This ModLock schema is not supported by this launcher."));
            m_preview->setEnabled(true);
            return;
        }
        QString name = pack.value("name").toString();
        if (name.isEmpty())
            name = m_repository->text().section('/', -1).section('.', 0, 0);
        QString version = pack.value("version").toString();
        if (version.isEmpty())
            version = result.value("revision").toString().left(12);
        pack.insert("_schema", schema);
        m_dialog->setModLockSource(pack, result.value("revision").toString(), name, version);

        QStringList details{tr("Build: %1 — %2").arg(name, version)};
        const auto components = pack.value("components").toArray();
        for (const auto& value : components) {
            const auto component = value.toObject();
            details.append(QStringLiteral("%1 %2").arg(component.value("id").toString(), component.value("version").toString()));
        }
        if (schema == 1) {
            details.append(tr("Schema 1: select Minecraft and loader manually on the Custom page."));
            m_dialog->setSuggestedPack(name, version);
        } else {
            QString minecraftVersion;
            QString loaderId;
            QString loaderVersion;
            bool invalid = false;
            for (const auto& value : components) {
                const auto component = value.toObject();
                const QString id = component.value("id").toString();
                const QString componentVersion = component.value("version").toString();
                if (componentVersion.isEmpty()) {
                    invalid = true;
                    break;
                }
                if (id == "net.minecraft") {
                    if (!minecraftVersion.isEmpty()) invalid = true;
                    minecraftVersion = componentVersion;
                } else if (id == "net.fabricmc.fabric-loader" || id == "org.quiltmc.quilt-loader" ||
                           id == "net.minecraftforge" || id == "net.neoforged") {
                    if (!loaderId.isEmpty()) invalid = true;
                    loaderId = id;
                    loaderVersion = componentVersion;
                } else {
                    invalid = true;
                }
                if (invalid) break;
            }
            if (minecraftVersion.isEmpty()) invalid = true;
            if (invalid) {
                m_dialog->setModLockSource({}, {}, {}, {});
                m_status->setText(tr("Schema %1 requires one Minecraft version and at most one supported loader. Unknown or duplicate components were found.").arg(schema));
                m_preview->setEnabled(true);
                return;
            }
            details.append(tr("Minecraft: %1").arg(minecraftVersion));
            details.append(loaderId.isEmpty() ? tr("Loader: none") : tr("Loader: %1 %2").arg(loaderId, loaderVersion));
            const int fileCount = lock.value("mods").toArray().size() + lock.value("files").toArray().size();
            details.append(tr("Files: %1").arg(fileCount));
            QString targetSummary;
            if (schema >= 3) {
                int clientCount = 0;
                int serverCount = 0;
                const auto countTargets = [&clientCount, &serverCount](const QJsonArray& entries) {
                    for (const auto& entry : entries) {
                        const auto targets = entry.toObject().value("targets").toArray();
                        for (const auto& target : targets) {
                            if (target.toString() == "client") ++clientCount;
                            if (target.toString() == "server") ++serverCount;
                        }
                    }
                };
                countTargets(lock.value("mods").toArray());
                countTargets(lock.value("files").toArray());
                targetSummary = tr("Targets: %1 client entries, %2 server entries").arg(clientCount).arg(serverCount);
                details.append(targetSummary);
            }
            auto versions = makeShared<SequentialTask>(tr("Checking exact ModLock profile versions"));
            versions->addTask(APPLICATION->metadataIndex()->loadVersion("net.minecraft", minecraftVersion));
            if (!loaderId.isEmpty())
                versions->addTask(APPLICATION->metadataIndex()->loadVersion(loaderId, loaderVersion));
            connect(versions.get(), &Task::finished, this, [this, versions, generation, pack, revision = result.value("revision").toString(), name, version, minecraftVersion, loaderId, loaderVersion, fileCount, schema, targetSummary]() mutable {
                if (generation != m_generation)
                    return;
                m_preview->setEnabled(true);
                if (!versions->wasSuccessful()) {
                    m_dialog->setModLockSource({}, {}, {}, {});
                    m_dialog->setSuggestedPack();
                    m_status->setText(tr("A requested Minecraft or loader version is unavailable: %1").arg(versions->failReason()));
                    return;
                }
                const auto minecraft = APPLICATION->metadataIndex()->getLoadedVersion("net.minecraft", minecraftVersion);
                BaseVersion::Ptr loader;
                if (!loaderId.isEmpty())
                    loader = APPLICATION->metadataIndex()->getLoadedVersion(loaderId, loaderVersion);
                m_dialog->setSuggestedPack(name, version,
                                           new ModLockCreationTask(minecraft, loaderId, loader, pack, revision, name, version, schema));
                m_status->setText(tr("Build: %1 — %2\nMinecraft: %3\nLoader: %4\nFiles: %5%6")
                                      .arg(name, version, minecraftVersion,
                                           loaderId.isEmpty() ? tr("none") : loaderId + " " + loaderVersion,
                                           QString::number(fileCount), targetSummary.isEmpty() ? QString() : QStringLiteral("\n") + targetSummary));
            });
            m_status->setText(details.join('\n') + tr("\nChecking exact component versions…"));
            versions->start();
        }
        if (schema == 1) {
            m_status->setText(details.join('\n'));
            m_preview->setEnabled(true);
        }
    });
    connect(bridge, &ModLockBridge::failed, this, [this, generation](const QString&, const QJsonObject& error) {
        if (generation != m_generation)
            return;
        m_status->setText(tr("Could not read the ModLock build."));
        m_preview->setEnabled(true);
        QMessageBox::critical(this, tr("ModLock import error"), error.value("message").toString());
    });
    const QJsonObject source{{"repository", repository},
                             {"branch", m_branch->text().trimmed()},
                             {"lock_path", m_lockPath->text().trimmed()},
                             {"mods_dir", "mods"}};
    if (!bridge->start("read", source)) {
        m_preview->setEnabled(true);
        m_status->setText(tr("Could not start the ModLock component."));
    }
}
