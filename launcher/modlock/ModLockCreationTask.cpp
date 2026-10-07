// SPDX-License-Identifier: GPL-3.0-only
#include "ModLockCreationTask.h"

#include "BaseInstance.h"
#include "ModLockBridge.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace {
QString preserveRecoveryBackups(const QString& message, const QString& stagingPath)
{
    const QString marker = QStringLiteral("backups preserved at ");
    const auto markerIndex = message.lastIndexOf(marker);
    if (markerIndex < 0)
        return message;

    const QString source = message.mid(markerIndex + marker.size()).trimmed();
    const QDir stagingParent = QFileInfo(stagingPath).dir();
    const QString destination = stagingParent.filePath(QStringLiteral("modlock-recovery-") + QUuid::createUuid().toString(QUuid::Id128));
    const QDir sourceDir(source);
    if (!sourceDir.exists() || !QDir().mkpath(destination))
        return message;

    bool copiedAny = false;
    bool copySucceeded = true;
    QDirIterator files(source, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString file = files.next();
        const QString target = QDir(destination).filePath(sourceDir.relativeFilePath(file));
        if (!QDir().mkpath(QFileInfo(target).dir().absolutePath()) || !QFile::copy(file, target)) {
            copySucceeded = false;
            break;
        }
        copiedAny = true;
    }
    if (copySucceeded && copiedAny)
        return message.left(markerIndex + marker.size()) + destination;

    QDir(destination).removeRecursively();
    return message;
}
}

ModLockCreationTask::ModLockCreationTask(BaseVersion::Ptr version,
                                         QString loader,
                                         BaseVersion::Ptr loaderVersion,
                                         QJsonObject pack,
                                         QString revision,
                                         QString packName,
                                         QString packVersion)
    : VanillaCreationTask(std::move(version), std::move(loader), std::move(loaderVersion))
    , m_pack(std::move(pack))
    , m_revision(std::move(revision))
    , m_packName(std::move(packName))
    , m_packVersion(std::move(packVersion))
{}

bool ModLockCreationTask::abort()
{
    if (m_bridge && m_bridge->isActive()) {
        m_bridge->cancel();
        m_abort = true;
        setAbortable(false);
        return true;
    }
    return VanillaCreationTask::abort();
}

std::unique_ptr<MinecraftInstance> ModLockCreationTask::createInstance()
{
    auto instance = VanillaCreationTask::createInstance();
    if (instance) {
        m_minecraftRoot = instance->gameRoot();
        instance->setManagedPack("modlock", m_pack.value("repository").toString(), m_packName, m_revision, m_packVersion);
        instance->settings()->set("ManagedPackURL", m_pack.value("repository").toString());
        instance->settings()->set("ModLockBranch", m_pack.value("branch").toString());
        instance->settings()->set("ModLockLockPath", m_pack.value("lock_path").toString());
    }
    return instance;
}

bool ModLockCreationTask::runPostInstall()
{
    if (m_minecraftRoot.isEmpty() || !QDir().mkpath(m_minecraftRoot)) {
        emitFailed(tr("Could not prepare the Minecraft directory for ModLock files."));
        return true;
    }

    m_bridge = std::make_unique<ModLockBridge>(m_minecraftRoot);
    connect(m_bridge.get(), &ModLockBridge::progress, this, [this](const QString&, const QString& message) { setStatus(message); });
    connect(m_bridge.get(), &ModLockBridge::completed, this, [this](const QString&, const QJsonObject&) {});
    connect(m_bridge.get(), &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) {
        m_installError = error;
    });
    connect(m_bridge.get(), &ModLockBridge::finished, this, [this] {
        setAbortable(false);
        if (m_abort || m_installError.value("code").toString() == "cancelled") {
            emitAborted();
            return;
        }
        if (!m_installError.isEmpty()) {
            const bool recovery = m_installError.value("code").toString() == "recovery_failed";
            const QString message = m_installError.value("message").toString(tr("ModLock installation failed."));
            emitFailed(recovery ? tr("ModLock installation recovery failed. Backups were preserved at:\n%1")
                                      .arg(preserveRecoveryBackups(message, m_stagingPath))
                                : m_installError.value("message").toString(tr("ModLock installation failed.")));
            return;
        }
        emitSucceeded();
    });

    setAbortable(true);
    m_installError = {};
    setAbortButtonText(tr("Cancel"));
    setDetails(tr("Installing ModLock files"));
    if (!m_bridge->start("install", {{"pack", m_pack}, {"revision", m_revision}})) {
        setAbortable(false);
        emitFailed(tr("Could not start the ModLock component."));
    }
    return true;
}
