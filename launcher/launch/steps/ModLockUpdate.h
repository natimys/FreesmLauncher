// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "launch/LaunchStep.h"

#include <QJsonObject>
#include <QJsonArray>

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
    void apply(const QString& revision, const QJsonArray& confirmedConflicts = {});
    void verifyAfterNetworkFailure(const QJsonObject& error);
    void onBridgeFinished();
    void showCheckError(const QJsonObject& error);
    void showOfflineChoice();

    ModLockBridge* m_bridge = nullptr;
    QString m_operation;
    QString m_revision;
    QJsonObject m_result;
    QJsonObject m_preview;
    QJsonObject m_error;
    QJsonObject m_networkError;
    QJsonArray m_confirmedConflicts;
    bool m_abortRequested = false;
};
