// SPDX-License-Identifier: GPL-3.0-only
#include "PackEditorReviewDialog.h"

#include <QDialogButtonBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QString changeLabel(const QJsonObject& change)
{
    QString label = change.value("name").toString();
    if (label.isEmpty()) label = change.value("filename").toString();
    if (label.isEmpty()) label = change.value("path").toString();
    if (label.isEmpty()) label = change.value("target").toString();
    if (label.isEmpty()) label = change.value("identity").toString();
    if (label.isEmpty()) label = change.value("id").toString();
    const QString action = change.value("action").toString();
    const QString targetId = change.value("target_id").toString();
    if (!action.isEmpty()) label += QStringLiteral(" — ") + action;
    if (!targetId.isEmpty()) label += QStringLiteral(" ") + targetId;
    const auto targets = change.value("targets").toArray();
    QStringList targetNames;
    for (const auto& target : targets) targetNames.append(target.toString());
    if (!targetNames.isEmpty()) label += QStringLiteral(" · ") + targetNames.join(QStringLiteral(", "));
    return label;
}
}

PackEditorReviewDialog::PackEditorReviewDialog(const QJsonObject& preview, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Review pack changes"));
    resize(680, 580);
    auto* layout = new QVBoxLayout(this);
    QString context = tr("Branch: %1").arg(preview.value("branch").toString(tr("Unknown")));
    if (preview.contains("revision")) context += tr("\nRevision: %1").arg(preview.value("revision").toString());
    layout->addWidget(new QLabel(context, this));

    auto* tree = new QTreeWidget(this);
    tree->setHeaderHidden(true);
    tree->setUniformRowHeights(true);
    int total = 0;
    const auto addGroup = [&tree, &total](const QString& title, const QJsonArray& items, const QString& action = QString(), bool countsAsChange = true) {
        if (items.isEmpty()) return;
        auto* group = new QTreeWidgetItem(tree, {QStringLiteral("%1 (%2)").arg(title).arg(items.size())});
        group->setExpanded(true);
        for (const auto& value : items) {
            const auto object = value.toObject();
            QString label = changeLabel(object);
            if (label.isEmpty()) label = object.value("message").toString();
            if (label.isEmpty() && value.isString()) label = value.toString();
            if (label.isEmpty()) label = QObject::tr("Resource details unavailable");
            if (!action.isEmpty()) label.prepend(action + QStringLiteral(": "));
            new QTreeWidgetItem(group, {label});
        }
        if (countsAsChange) total += items.size();
    };
    const auto mods = preview.value("mods").toObject();
    const auto files = preview.value("files").toObject();
    addGroup(tr("Added mods"), mods.value("added").toArray());
    addGroup(tr("Updated mods"), mods.value("updated").toArray());
    addGroup(tr("Removed mods"), mods.value("removed").toArray());
    addGroup(tr("Added files"), files.value("added").toArray());
    addGroup(tr("Updated files"), files.value("updated").toArray());
    addGroup(tr("Removed files"), files.value("removed").toArray());
    addGroup(tr("Target changes"), preview.value("target_changes").toArray());
    addGroup(tr("Excluded from this publish"), preview.value("ignored").toArray());
    addGroup(tr("Validation warnings"), preview.value("warnings").toArray(), {}, false);
    if (total == 0) layout->addWidget(new QLabel(tr("The preview reports no pack changes."), this));
    layout->addWidget(tree, 1);

    auto* formLabel = new QLabel(tr("Commit message"), this);
    layout->addWidget(formLabel);
    m_commitMessage = new QLineEdit(preview.value("commit_message").toString(), this);
    m_commitMessage->setPlaceholderText(tr("Describe these changes"));
    layout->addWidget(m_commitMessage);

    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(QDialogButtonBox::Cancel);
    auto* publish = buttons->addButton(tr("Publish changes"), QDialogButtonBox::AcceptRole);
    const bool hasChanges = total > 0 && !preview.value("preview_id").toString().isEmpty();
    connect(m_commitMessage, &QLineEdit::textChanged, this, [publish, hasChanges](const QString& message) {
        publish->setEnabled(hasChanges && !message.trimmed().isEmpty());
    });
    publish->setEnabled(hasChanges && !m_commitMessage->text().trimmed().isEmpty());
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(publish, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(buttons);
}

QString PackEditorReviewDialog::commitMessage() const { return m_commitMessage->text().trimmed(); }
