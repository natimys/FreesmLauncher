// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonObject>

#include "minecraft/VanillaInstanceCreationTask.h"

class ModLockBridge;

class ModLockCreationTask final : public VanillaCreationTask {
    Q_OBJECT

   public:
    ModLockCreationTask(BaseVersion::Ptr version,
                        QString loader,
                        BaseVersion::Ptr loaderVersion,
                        QJsonObject pack,
                        QString revision,
                        QString packName,
                        QString packVersion);

    bool abort() override;
    std::unique_ptr<MinecraftInstance> createInstance() override;

   protected:
    bool runPostInstall() override;

   private:
    QJsonObject m_pack;
    QString m_revision;
    QString m_packName;
    QString m_packVersion;
    std::unique_ptr<ModLockBridge> m_bridge;
};
