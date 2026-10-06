// SPDX-License-Identifier: GPL-3.0-only
#include "ModLockImportPage.h"

#include "modlock/ModLockBridge.h"
#include "ui/dialogs/NewInstanceDialog.h"

#include <QDir>
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
      m_lockPath(new QLineEdit("mod.lock", this)), m_status(new QLabel(this)), m_preview(new QPushButton(tr("Preview build"), this))
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_repository->setPlaceholderText(tr("https://github.com/owner/repository"));
    form->addRow(tr("Git repository"), m_repository);
    form->addRow(tr("Branch"), m_branch);
    form->addRow(tr("Lock file path"), m_lockPath);
    layout->addLayout(form);
    layout->addWidget(m_preview);
    layout->addWidget(m_status);
    layout->addStretch();
    connect(m_preview, &QPushButton::clicked, this, &ModLockImportPage::preview);
}

void ModLockImportPage::setRepository(const QString& repository)
{
    m_repository->setText(repository);
    preview();
}

void ModLockImportPage::preview()
{
    if (m_bridge && m_bridge->isActive())
        return;
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
    delete m_bridge;
    m_bridge = new ModLockBridge(root, this);
    m_preview->setEnabled(false);
    m_status->setText(tr("Reading the build from Git…"));
    connect(m_bridge, &ModLockBridge::progress, this, [this](const QString&, const QString& message) { m_status->setText(message); });
    connect(m_bridge, &ModLockBridge::completed, this, [this](const QString&, const QJsonObject& result) {
        const QJsonObject lock = result.value("lock").toObject();
        const QJsonObject pack = lock.value("pack").toObject();
        if (lock.isEmpty() || pack.isEmpty()) {
            m_status->setText(tr("The repository did not return a valid ModLock manifest."));
            m_preview->setEnabled(true);
            return;
        }
        if (lock.value("schema").toInt(1) != 1) {
            m_status->setText(tr("This build uses a newer ModLock schema that this launcher cannot install yet."));
            m_preview->setEnabled(true);
            return;
        }
        QString name = pack.value("name").toString();
        if (name.isEmpty())
            name = m_repository->text().section('/', -1).section('.', 0, 0);
        QString version = pack.value("version").toString();
        if (version.isEmpty())
            version = result.value("revision").toString().left(12);
        m_dialog->setModLockSource(pack, result.value("revision").toString(), name, version);

        QStringList details{tr("Build: %1 — %2").arg(name, version)};
        const auto components = pack.value("components").toArray();
        for (const auto& value : components) {
            const auto component = value.toObject();
            details.append(QStringLiteral("%1 %2").arg(component.value("id").toString(), component.value("version").toString()));
        }
        if (lock.value("schema").toInt(1) == 1)
            details.append(tr("Schema 1: select Minecraft and loader manually on the Custom page."));
        m_status->setText(details.join('\n'));
        m_preview->setEnabled(true);
    });
    connect(m_bridge, &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) {
        m_status->setText(tr("Could not read the ModLock build."));
        m_preview->setEnabled(true);
        QMessageBox::critical(this, tr("ModLock import error"), error.value("message").toString());
    });
    const QJsonObject source{{"repository", repository},
                             {"branch", m_branch->text().trimmed()},
                             {"lock_path", m_lockPath->text().trimmed()},
                             {"mods_dir", "mods"}};
    if (!m_bridge->start("read", source)) {
        m_preview->setEnabled(true);
        m_status->setText(tr("Could not start the ModLock component."));
    }
}
