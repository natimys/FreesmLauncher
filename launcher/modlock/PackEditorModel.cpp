// SPDX-License-Identifier: GPL-3.0-only
#include "PackEditorModel.h"

#include <QJsonValue>
#include <QStringList>

QString packEditorStatusSummary(const QJsonObject& entry)
{
    QStringList statuses;
    for (const auto& value : entry.value("target_states").toArray()) {
        const auto targetState = value.toObject();
        const auto target = targetState.value("target_id").toString();
        const auto status = targetState.value("status").toString();
        if (!target.isEmpty() && !status.isEmpty())
            statuses.append(QStringLiteral("%1: %2").arg(target, status));
    }
    return statuses.isEmpty() ? entry.value("status").toString() : statuses.join(QStringLiteral("; "));
}

bool packEditorPageShouldDisplay(bool isMinecraftInstance)
{
    return isMinecraftInstance;
}

bool modLockImportSchemaSupported(int schema)
{
    return schema == 1 || schema == 2 || schema == 3;
}

QJsonObject packEditorSetModTargetsParams(const QString& id, const QJsonArray& targets)
{
    return {{"id", id}, {"targets", targets}};
}

bool PackEditorAuthorState::parse(const QJsonObject& result, PackEditorAuthorState* state, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!state)
        return fail(QStringLiteral("No author state destination was provided."));
    if (!result.value("lock").isObject() && !result.value("lock").isNull())
        return fail(QStringLiteral("Author state contains an invalid lock value."));
    if (!result.value("settings").isObject() || !result.value("mods").isArray() || !result.value("files").isArray() ||
        !result.value("tracked_paths").isArray() || !result.value("branch").isString() || !result.value("dirty").isBool())
        return fail(QStringLiteral("Author state is missing required fields."));

    state->lock = result.value("lock");
    state->settings = result.value("settings").toObject();
    state->mods = result.value("mods").toArray();
    state->files = result.value("files").toArray();
    state->trackedPaths = result.value("tracked_paths").toArray();
    state->branch = result.value("branch").toString();
    state->dirty = result.value("dirty").toBool();
    if (error)
        error->clear();
    return true;
}

PackEditorTargetModel::PackEditorTargetModel(QString target, QObject* parent)
    : QAbstractListModel(parent), m_target(std::move(target))
{}

int PackEditorTargetModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_visibleEntries.size();
}

QVariant PackEditorTargetModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0)
        return {};
    if (index.row() >= m_visibleEntries.size())
        return {};
    const auto entry = m_visibleEntries.at(index.row());
    if (role == EntryRole)
        return entry;
    if (role == IdentityRole)
        return identity(entry);
    if (role == SharedRole)
        return isShared(entry);
    if (role == Qt::DecorationRole) {
        const auto key = identity(entry);
        if (m_icons.contains(key))
            return m_icons.value(key);
    }
    if (role == Qt::DisplayRole) {
        for (const auto& key : {QStringLiteral("name"), QStringLiteral("id"), QStringLiteral("filename"), QStringLiteral("path")}) {
            const auto value = entry.value(key).toString();
            if (!value.isEmpty())
                return value;
        }
    }
    return {};
}

QHash<int, QByteArray> PackEditorTargetModel::roleNames() const
{
    auto roles = QAbstractListModel::roleNames();
    roles.insert(EntryRole, "entry");
    roles.insert(IdentityRole, "identity");
    roles.insert(SharedRole, "shared");
    return roles;
}

void PackEditorTargetModel::setEntries(const QJsonArray& entries)
{
    beginResetModel();
    m_entries = entries;
    rebuildVisibleEntries();
    endResetModel();
}

void PackEditorTargetModel::setShowShared(bool showShared)
{
    if (m_showShared == showShared)
        return;
    beginResetModel();
    m_showShared = showShared;
    rebuildVisibleEntries();
    endResetModel();
}

void PackEditorTargetModel::setIcon(const QString& entryIdentity, const QIcon& icon)
{
    if (entryIdentity.isEmpty() || icon.isNull() || m_icons.value(entryIdentity).cacheKey() == icon.cacheKey())
        return;
    m_icons.insert(entryIdentity, icon);
    for (int row = 0; row < m_visibleEntries.size(); ++row) {
        if (identity(m_visibleEntries.at(row)) == entryIdentity) {
            const auto changed = index(row, 0);
            emit dataChanged(changed, changed, {Qt::DecorationRole});
        }
    }
}

QJsonObject PackEditorTargetModel::entryAt(int row) const
{
    return row >= 0 && row < m_visibleEntries.size() ? m_visibleEntries.at(row) : QJsonObject();
}

bool PackEditorTargetModel::entryMatches(const QJsonObject& entry) const
{
    const auto targetsValue = entry.value("targets");
    if (!targetsValue.isArray())
        return m_target == QStringLiteral("client");  // schema 1/2 are legacy client-only.
    for (const auto& value : targetsValue.toArray()) {
        if (value.toString() == m_target)
            return true;
    }
    return false;
}

bool PackEditorTargetModel::isShared(const QJsonObject& entry) const
{
    bool client = false;
    bool server = false;
    for (const auto& value : entry.value("targets").toArray()) {
        client |= value.toString() == QStringLiteral("client");
        server |= value.toString() == QStringLiteral("server");
    }
    return client && server;
}

QString PackEditorTargetModel::identity(const QJsonObject& entry) const
{
    for (const auto& key : {QStringLiteral("identity"), QStringLiteral("id"), QStringLiteral("path"), QStringLiteral("filename")}) {
        const auto value = entry.value(key).toString();
        if (!value.isEmpty())
            return value;
    }
    return {};
}

void PackEditorTargetModel::rebuildVisibleEntries()
{
    m_visibleEntries.clear();
    m_visibleEntries.reserve(m_entries.size());
    for (const auto& value : m_entries) {
        if (!value.isObject())
            continue;
        const auto entry = value.toObject();
        if (!entryMatches(entry) || (!m_showShared && isShared(entry)))
            continue;
        m_visibleEntries.append(entry);
    }
}
