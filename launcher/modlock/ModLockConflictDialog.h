// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

namespace ModLockConflictDialog {
inline bool confirm(QWidget* parent, const QJsonArray& conflicts, const QJsonArray& changes, QJsonArray* confirmations)
{
    QStringList rows;
    bool confirmable = !conflicts.isEmpty();
    for (const auto& value : conflicts) {
        const auto conflict = value.toObject();
        const QString target = conflict.value("target").toString();
        const QString reason = conflict.value("reason").toString();
        const QString hash = conflict.value("sha256").toString();
        if ((reason != "existing file would be replaced" && reason != "locally modified managed file") || hash.isEmpty())
            confirmable = false;
        QString action = QObject::tr("Replace local file");
        for (const auto& changeValue : changes) {
            const auto change = changeValue.toObject();
            if (change.value("target").toString() == target && change.value("action").toString() == "remove")
                action = QObject::tr("Delete local file");
        }
        rows.append(QStringLiteral("%1 — %2%3")
                        .arg(target.toHtmlEscaped(), action.toHtmlEscaped(),
                             hash.isEmpty() ? QString() : QObject::tr(" (local SHA-256 %1…)").arg(hash.left(12))));
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
