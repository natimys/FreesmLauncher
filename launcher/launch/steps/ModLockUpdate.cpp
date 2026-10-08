// SPDX-License-Identifier: GPL-3.0-only
#include "ModLockUpdate.h"

#include "BaseInstance.h"
#include "modlock/ModLockConflictDialog.h"
#include "modlock/ModLockBridge.h"
#include "launch/LaunchTask.h"

#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

namespace {
QString modLockRoot(BaseInstance* instance)
{
    if (!instance)
        return {};
    return QFileInfo::exists(QDir(instance->instanceRoot()).filePath(QStringLiteral("mod.lock")))
               ? instance->instanceRoot() : instance->gameRoot();
}
bool hasAuthorConfig(BaseInstance* instance)
{
    return instance && (QFileInfo::exists(QDir(instance->instanceRoot()).filePath(QStringLiteral(".modlock/author.toml"))) ||
                        QFileInfo::exists(QDir(instance->gameRoot()).filePath(QStringLiteral(".modlock/author.toml"))));
}
}  // namespace

ModLockUpdate::ModLockUpdate(LaunchTask* parent) : LaunchStep(parent) {}

bool ModLockUpdate::abort()
{
    if (!m_bridge || !m_bridge->isActive()) {
        m_abortRequested = true;
        emitAborted();
        return true;
    }
    m_bridge->cancel();
    m_abortRequested = true;
    setAbortable(false);
    return true;
}

void ModLockUpdate::executeTask()
{
    auto* instance = m_parent->instance();
    if (!instance || instance->getManagedPackType() != "modlock" ||
        hasAuthorConfig(instance)) {
        emitSucceeded();
        return;
    }
    check();
}

void ModLockUpdate::check()
{
    m_operation = "check";
    m_result = {};
    m_error = {};
    m_revision.clear();
    setStatus(tr("Checking ModLock updates"));
    setDetails(tr("Reading the pinned build revision"));
    setAbortable(true);
    m_bridge = new ModLockBridge(modLockRoot(m_parent->instance()), this);
    connect(m_bridge, &ModLockBridge::progress, this, [this](const QString&, const QString& message) { setDetails(message); });
    connect(m_bridge, &ModLockBridge::completed, this, [this](const QString&, const QJsonObject& result) { m_result = result; });
    connect(m_bridge, &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) { m_error = error; });
    connect(m_bridge, &ModLockBridge::finished, this, &ModLockUpdate::onBridgeFinished);
    auto* instance = m_parent->instance();
    if (!m_bridge->start("check", {{"target_roots", modLockTargetRoots(instance->gameRoot(), instance->instanceRoot())}})) {
        m_bridge->deleteLater();
        m_bridge = nullptr;
        m_error = {{"code", "component_unavailable"}, {"message", tr("Could not start the ModLock component.")}};
        onBridgeFinished();
    }
}

void ModLockUpdate::apply(const QString& revision, const QJsonArray& confirmedConflicts)
{
    m_operation = "apply";
    m_confirmedConflicts = confirmedConflicts;
    m_error = {};
    m_revision = revision;
    setStatus(tr("Applying ModLock update"));
    setDetails(tr("Installing previewed revision %1").arg(revision));
    setAbortable(true);
    m_bridge = new ModLockBridge(modLockRoot(m_parent->instance()), this);
    connect(m_bridge, &ModLockBridge::progress, this, [this](const QString&, const QString& message) { setDetails(message); });
    connect(m_bridge, &ModLockBridge::completed, this, [this](const QString&, const QJsonObject& result) { m_result = result; });
    connect(m_bridge, &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) { m_error = error; });
    connect(m_bridge, &ModLockBridge::finished, this, &ModLockUpdate::onBridgeFinished);
    QJsonObject params{{"revision", revision}};
    if (!m_confirmedConflicts.isEmpty())
        params.insert("confirmed_conflicts", m_confirmedConflicts);
    params.insert("target_roots", modLockTargetRoots(m_parent->instance()->gameRoot(), m_parent->instance()->instanceRoot()));
    if (!m_bridge->start("apply", params)) {
        m_bridge->deleteLater();
        m_bridge = nullptr;
        m_error = {{"code", "component_unavailable"}, {"message", tr("Could not start the ModLock component.")}};
        onBridgeFinished();
    }
}

void ModLockUpdate::onBridgeFinished()
{
    setAbortable(false);
    if (m_bridge) {
        m_bridge->deleteLater();
        m_bridge = nullptr;
    }
    if (m_abortRequested && m_error.value("code").toString() == "cancelled") {
        emitAborted();
        return;
    }
    if (!m_error.isEmpty()) {
        if (m_operation == "apply" && m_error.value("code").toString() == "file_conflict") {
            QJsonArray confirmations;
            const auto conflicts = m_error.value("details").toObject().value("conflicts").toArray();
            if (conflicts.isEmpty()) {
                apply(m_revision);
            } else if (ModLockConflictDialog::confirm(nullptr, conflicts, m_preview.value("managed_files").toArray(), &confirmations)) {
                apply(m_revision, confirmations);
            } else {
                m_confirmedConflicts = {};
                if (m_preview.value("local_installation").toObject().value("needs_recovery").toBool()) {
                    emitAborted();
                } else {
                    setStatus(tr("Local files were kept. The update was deferred."));
                    emitSucceeded();
                }
            }
            return;
        }
        if (m_operation == "check") {
            showCheckError(m_error);
            return;
        }
        if (m_operation == "verify") {
            QMessageBox box(QMessageBox::Critical, tr("ModLock verification failed"),
                            tr("Could not verify the installed files, so launch is blocked. Retry the online check or cancel launch.\n\n%1\n\n%2")
                                .arg(m_error.value("message").toString(), m_networkError.value("message").toString()),
                            QMessageBox::NoButton);
            auto* retry = box.addButton(tr("Retry"), QMessageBox::AcceptRole);
            box.addButton(tr("Cancel launch"), QMessageBox::RejectRole);
            box.setDefaultButton(qobject_cast<QPushButton*>(retry));
            box.exec();
            if (box.clickedButton() == retry)
                check();
            else
                emitAborted();
            return;
        }
        const bool recovery = m_error.value("code").toString() == "recovery_failed";
        QMessageBox::critical(nullptr, recovery ? tr("ModLock recovery failed") : tr("ModLock update failed"),
                              recovery ? m_error.value("message").toString()
                                       : tr("The update was not applied. Launch is blocked until the update succeeds or recovery is verified.\n\n%1")
                                             .arg(m_error.value("message").toString()));
        emitFailed(m_error.value("message").toString());
        return;
    }

    if (m_abortRequested && m_operation != "apply") {
        emitAborted();
        return;
    }

    if (m_operation == "verify") {
        const bool needsRecovery = m_result.value("needs_recovery").toBool();
        if (needsRecovery) {
            const auto fileList = [](const QJsonValue& value) {
                QStringList names;
                for (const auto& item : value.toArray())
                    names.append(item.toString());
                return names;
            };
            const QStringList missing = fileList(m_result.value("missing"));
            const QStringList damaged = fileList(m_result.value("damaged"));
            QMessageBox box(QMessageBox::Critical, tr("ModLock files need recovery"),
                            tr("The installed revision cannot be launched because managed files are missing or damaged. Retry the network check or cancel launch.\n\nMissing: %1\nDamaged: %2")
                                .arg(missing.join(", "), damaged.join(", ")),
                            QMessageBox::NoButton);
            auto* retry = box.addButton(tr("Retry"), QMessageBox::AcceptRole);
            box.addButton(tr("Cancel launch"), QMessageBox::RejectRole);
            box.setDefaultButton(qobject_cast<QPushButton*>(retry));
            box.exec();
            if (box.clickedButton() == retry)
                check();
            else
                emitAborted();
            return;
        }
        showOfflineChoice();
        return;
    }

    if (m_operation == "check") {
        m_preview = m_result;
        const QString revision = m_result.value("revision").toString();
        const bool needsRecovery = m_result.value("local_installation").toObject().value("needs_recovery").toBool();
        const auto diff = m_result.value("diff").toObject();
        const bool hasManagedChanges = !m_result.value("managed_files").toArray().isEmpty();
        const bool hasConflicts = !m_result.value("conflicts").toArray().isEmpty();
        auto* instance = m_parent->instance();
        if (revision.isEmpty()) {
            emitFailed(tr("ModLock returned no Git revision."));
            return;
        }
        if (revision == instance->getManagedPackVersionID() && !needsRecovery && !hasManagedChanges && !hasConflicts &&
            diff.value("added").toArray().isEmpty() && diff.value("removed").toArray().isEmpty() &&
            diff.value("updated").toArray().isEmpty()) {
            setStatus(tr("ModLock is up to date"));
            emitSucceeded();
            return;
        }
        apply(revision);
        return;
    }

    const QString appliedRevision = m_result.value("revision").toString();
    if (appliedRevision.isEmpty()) {
        emitFailed(tr("ModLock applied an update without returning its Git revision."));
        return;
    }
    auto* instance = m_parent->instance();
    instance->settings()->set("ManagedPackVersionID", appliedRevision);
    const QString packVersion = m_preview.value("lock").toObject().value("pack").toObject().value("version").toString();
    if (!packVersion.isEmpty())
        instance->settings()->set("ManagedPackVersionName", packVersion);
    setStatus(tr("ModLock build %1 installed at Git revision %2").arg(packVersion.isEmpty() ? tr("(version not set)") : packVersion,
                                                                          appliedRevision.left(12)));
    if (m_abortRequested) {
        emitAborted();
        return;
    }
    emitSucceeded();
}

void ModLockUpdate::showCheckError(const QJsonObject& error)
{
    const QString message = error.value("message").toString(tr("ModLock could not check for updates."));
    if (error.value("code").toString() != "network") {
        QMessageBox::critical(nullptr, tr("ModLock check failed"), message);
        emitFailed(message);
        return;
    }

    verifyAfterNetworkFailure(error);
}

void ModLockUpdate::verifyAfterNetworkFailure(const QJsonObject& error)
{
    m_networkError = error;
    m_operation = "verify";
    m_result = {};
    m_error = {};
    setStatus(tr("Checking installed ModLock files before offering offline launch"));
    setDetails(tr("Verifying local managed mods"));
    setAbortable(true);
    m_bridge = new ModLockBridge(modLockRoot(m_parent->instance()), this);
    connect(m_bridge, &ModLockBridge::completed, this, [this](const QString&, const QJsonObject& result) { m_result = result; });
    connect(m_bridge, &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& result) { m_error = result; });
    connect(m_bridge, &ModLockBridge::finished, this, &ModLockUpdate::onBridgeFinished);
    auto* instance = m_parent->instance();
    if (!m_bridge->start("verify", {{"target_roots", modLockTargetRoots(instance->gameRoot(), instance->instanceRoot())}})) {
        m_bridge->deleteLater();
        m_bridge = nullptr;
        QMessageBox::critical(nullptr, tr("ModLock verification failed"), tr("Could not start local file verification. Launch is blocked."));
        emitFailed(tr("Could not verify installed ModLock files."));
    }
}

void ModLockUpdate::showOfflineChoice()
{
    const auto local = m_result;
    const bool hasUnverified = !local.value("unverified").toArray().isEmpty();
    const bool hasLocalChanges = !local.value("managed_changed").toArray().isEmpty();
    QMessageBox box(QMessageBox::Warning, tr("ModLock is unavailable"),
                    hasLocalChanges
                        ? tr("Could not check for an update. Local changes to managed configs or scripts were found and will be preserved. The installed files passed local recovery checks, so you can retry, cancel launch, or play the installed revision.\n\n%1")
                              .arg(m_networkError.value("message").toString())
                        : hasUnverified
                        ? tr("Could not check for an update. Managed files are present and files with lock hashes match; some older entries have no hash, so their contents could not be confirmed. You can retry, cancel launch, or play the installed revision.\n\n%1")
                              .arg(m_networkError.value("message").toString())
                        : tr("Could not check for an update. The installed managed files passed local verification. You can retry, cancel launch, or play the installed revision.\n\n%1")
                              .arg(m_networkError.value("message").toString()),
                    QMessageBox::NoButton);
    auto* retry = box.addButton(tr("Retry"), QMessageBox::AcceptRole);
    auto* launchInstalled = box.addButton(tr("Launch installed version"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancel launch"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(retry));
    box.exec();
    if (box.clickedButton() == retry)
        check();
    else if (box.clickedButton() == launchInstalled)
        emitSucceeded();
    else
        emitAborted();
}
