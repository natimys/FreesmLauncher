// SPDX-License-Identifier: GPL-3.0-only
#include "ModLockCreationTask.h"

#include "BaseInstance.h"
#include "ModLockBridge.h"

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
        if (!m_bridge->cancel())
            return false;
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
        instance->setManagedPack("modlock", m_pack.value("repository").toString(), m_packName, m_revision, m_packVersion);
        instance->settings()->set("ManagedPackURL", m_pack.value("repository").toString());
    }
    return instance;
}

bool ModLockCreationTask::runPostInstall()
{
    m_bridge = std::make_unique<ModLockBridge>(m_stagingPath);
    connect(m_bridge.get(), &ModLockBridge::progress, this, [this](const QString&, const QString& message) { setStatus(message); });
    connect(m_bridge.get(), &ModLockBridge::completed, this, [this](const QString&, const QJsonObject&) {
        setAbortable(false);
        emitSucceeded();
    });
    connect(m_bridge.get(), &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) {
        setAbortable(false);
        const QString message = error.value("message").toString(tr("ModLock installation failed."));
        if (m_abort || error.value("code").toString() == "cancelled")
            emitAborted();
        else
            emitFailed(message);
    });

    setAbortable(true);
    setAbortButtonText(tr("Cancel"));
    setDetails(tr("Installing ModLock files"));
    if (!m_bridge->start("install", {{"pack", m_pack}, {"revision", m_revision}})) {
        setAbortable(false);
        emitFailed(tr("Could not start the ModLock component."));
    }
    return true;
}
