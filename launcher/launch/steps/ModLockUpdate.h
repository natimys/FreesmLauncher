// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "launch/LaunchStep.h"

#include <QJsonObject>

class ModLockBridge;

class ModLockUpdate final : public LaunchStep {
    Q_OBJECT

   public:
    explicit ModLockUpdate(LaunchTask* parent);
    bool canAbort() const override { return true; }
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    void check();
    void apply(const QString& revision);
    void onBridgeFinished();
    void showCheckError(const QJsonObject& error);

    ModLockBridge* m_bridge = nullptr;
    QString m_operation;
    QString m_revision;
    QJsonObject m_result;
    QJsonObject m_error;
    bool m_abortRequested = false;
};
