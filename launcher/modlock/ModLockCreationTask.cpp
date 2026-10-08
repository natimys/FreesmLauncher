// SPDX-License-Identifier: GPL-3.0-only
#include "ModLockCreationTask.h"

#include "BaseInstance.h"
#include "ModLockBridge.h"
#include "ModLockConflictDialog.h"

#include <QDir>

ModLockCreationTask::ModLockCreationTask(BaseVersion::Ptr version,
                                         QString loader,
                                         BaseVersion::Ptr loaderVersion,
                                         QJsonObject pack,
                                         QString revision,
                                         QString packName,
                                         QString packVersion,
                                         int schema)
    : VanillaCreationTask(std::move(version), std::move(loader), std::move(loaderVersion))
    , m_pack(std::move(pack))
    , m_revision(std::move(revision))
    , m_packName(std::move(packName))
    , m_packVersion(std::move(packVersion))
    , m_schema(schema)
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
        m_instanceRoot = instance->instanceRoot();
        instance->setManagedPack("modlock", m_pack.value("repository").toString(), m_packName, m_revision, m_packVersion);
        instance->settings()->set("ManagedPackURL", m_pack.value("repository").toString());
        instance->settings()->set("ModLockBranch", m_pack.value("branch").toString());
        instance->settings()->set("ModLockLockPath", m_pack.value("lock_path").toString());
    }
    return instance;
}

bool ModLockCreationTask::runPostInstall()
{
    if (m_minecraftRoot.isEmpty() || !QDir().mkpath(m_minecraftRoot) ||
        (m_schema >= 3 && !QDir().mkpath(QDir(m_instanceRoot).filePath(QStringLiteral("server"))))) {
        emitFailed(tr("Could not prepare the Minecraft directory for ModLock files."));
        return true;
    }

    startInstall();
    return true;
}

void ModLockCreationTask::startInstall(const QJsonArray& confirmedConflicts)
{
    if (m_bridge) {
        m_bridge.release()->deleteLater();
    }
    m_installError = {};
        m_bridge = std::make_unique<ModLockBridge>(m_schema >= 3 ? m_instanceRoot : m_minecraftRoot);
    connect(m_bridge.get(), &ModLockBridge::progress, this, [this](const QString&, const QString& message) { setStatus(message); });
    connect(m_bridge.get(), &ModLockBridge::completed, this, [this](const QString&, const QJsonObject&) {});
    connect(m_bridge.get(), &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) {
        m_installError = error;
    });
    connect(m_bridge.get(), &ModLockBridge::finished, this, [this] {
        setAbortable(false);
        const QString code = m_installError.value("code").toString();
        if (code == "recovery_failed") {
            setPreserveStagingOnFailure(true);
            const QString message = m_installError.value("message").toString(tr("Recovery details are unavailable."));
            emitFailed(tr("ModLock installation recovery failed. Recovery materials remain in the staged instance at:\n%1\n\n%2")
                           .arg(m_stagingPath, message));
            return;
        }
        if (m_abort || m_installError.value("code").toString() == "cancelled") {
            emitAborted();
            return;
        }
        if (m_installError.value("code").toString() == "file_conflict") {
            QJsonArray confirmations;
            const auto conflicts = m_installError.value("details").toObject().value("conflicts").toArray();
            if (ModLockConflictDialog::confirm(nullptr, conflicts, {}, &confirmations)) {
                startInstall(confirmations);
            } else {
                emitAborted();
            }
            return;
        }
        if (!m_installError.isEmpty()) {
            emitFailed(m_installError.value("message").toString(tr("ModLock installation failed.")));
            return;
        }
        emitSucceeded();
    });

    setAbortable(true);
    m_installError = {};
    setAbortButtonText(tr("Cancel"));
    setDetails(tr("Installing ModLock files"));
    QJsonObject params{{"pack", m_pack}, {"revision", m_revision}};
    if (m_schema >= 3)
        params.insert("target_roots", modLockTargetRoots(m_minecraftRoot, m_instanceRoot));
    if (!confirmedConflicts.isEmpty())
        params.insert("confirmed_conflicts", confirmedConflicts);
    if (!m_bridge->start("install", params)) {
        setAbortable(false);
        emitFailed(tr("Could not start the ModLock component."));
    }
}
