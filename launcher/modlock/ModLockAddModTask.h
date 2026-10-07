// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>

#include "tasks/Task.h"

class ModLockBridge;
class NetJob;

class ModLockAddModTask final : public Task {
    Q_OBJECT

   public:
    ModLockAddModTask(QString gameRoot, QJsonObject mod, QJsonArray targets, QJsonObject targetRoots, QObject* parent = nullptr);
    bool canAbort() const override { return true; }
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    void onDownloadSucceeded();
    void onDownloadFailed(const QString& message);
    void onBridgeFinished();
    void cleanupStage();

    QString m_gameRoot;
    QString m_stageDirectory;
    QString m_stagedPath;
    QJsonObject m_mod;
    QJsonArray m_targets;
    QJsonObject m_targetRoots;
    QPointer<NetJob> m_download;
    QPointer<ModLockBridge> m_bridge;
    QString m_bridgeError;
    bool m_bridgeFailed = false;
    bool m_cancelRequested = false;
    bool m_stageOwned = false;
};
