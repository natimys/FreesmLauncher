// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

namespace ModLockConflictDialog {
inline bool isConfirmable(const QJsonObject& conflict)
{
    const QString kind = conflict.value("kind").toString();
    if (!kind.isEmpty())
        return (kind == "locally_modified" || kind == "existing_unmanaged") && !conflict.value("sha256").toString().isEmpty();

    // Compatibility for older bridge payloads that predate machine-readable kinds.
    const QString reason = conflict.value("reason").toString();
    return (reason == "existing file would be replaced" || reason == "locally modified managed file") &&
           !conflict.value("sha256").toString().isEmpty();
}

inline bool confirm(QWidget* parent, const QJsonArray& conflicts, const QJsonArray& changes, QJsonArray* confirmations)
{
    QStringList rows;
    bool confirmable = !conflicts.isEmpty();
    for (const auto& value : conflicts) {
        const auto conflict = value.toObject();
        const QString target = conflict.value("target").toString();
        const QString hash = conflict.value("sha256").toString();
        if (!isConfirmable(conflict))
            confirmable = false;
        QString action = QObject::tr("Replace local file");
        for (const auto& changeValue : changes) {
            const auto change = changeValue.toObject();
            if (change.value("target").toString() == target && change.value("action").toString() == "remove")
                action = QObject::tr("Delete local file");
        }
        const QString message = conflict.value("message").toString();
        rows.append(QStringLiteral("%1 — %2%3%4")
                        .arg(target.toHtmlEscaped(), action.toHtmlEscaped(),
                             hash.isEmpty() ? QString() : QObject::tr(" (local SHA-256 %1…)").arg(hash.left(12)),
                             message.isEmpty() ? QString() : QStringLiteral(" — %1").arg(message.toHtmlEscaped())));
    }
    QMessageBox box(QMessageBox::Warning, QObject::tr("Local files conflict with this build"),
                    QObject::tr("Choose whether to apply the build files or keep your local files and defer the entire update.\n\n%1")
                        .arg(rows.join('\n')),
                    QMessageBox::NoButton, parent);
    auto* apply = box.addButton(QObject::tr("Apply build version"), QMessageBox::AcceptRole);
    auto* defer = box.addButton(QObject::tr("Keep local files and defer update"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(apply));
    apply->setEnabled(confirmable);
    box.exec();
    if (box.clickedButton() != apply || !confirmations)
        return false;
    *confirmations = QJsonArray();
    for (const auto& value : conflicts) {
        const auto conflict = value.toObject();
        confirmations->append(QJsonObject{{"target", conflict.value("target")}, {"sha256", conflict.value("sha256")}});
    }
    Q_UNUSED(defer);
    return true;
}
}  // namespace ModLockConflictDialog
