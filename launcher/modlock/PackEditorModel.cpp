// SPDX-License-Identifier: GPL-3.0-only
#include "PackEditorModel.h"

#include <QJsonValue>
#include <QCollator>
#include <QSet>
#include <QStringList>
#include <utility>

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

bool packEditorPageShouldDisplay(bool isMinecraftInstance, bool hasAuthorWorkspace, bool canPromoteWorkspace)
{
    return isMinecraftInstance && (hasAuthorWorkspace || canPromoteWorkspace);
}

bool modLockImportSchemaSupported(int schema)
{
    return schema == 1 || schema == 2 || schema == 3;
}

QJsonObject packEditorSetModTargetsParams(const QString& id, const QJsonArray& targets)
{
    return {{"id", id}, {"targets", targets}};
}

bool packEditorTargetsAreValid(const QJsonArray& targets)
{
    bool client = false;
    bool server = false;
    for (const auto& target : targets) {
        if (target.toString() == QStringLiteral("client")) {
            if (client) return false;
            client = true;
        } else if (target.toString() == QStringLiteral("server")) {
            if (server) return false;
            server = true;
        }
        else return false;
    }
    return client || server;
}

PackEditorInventoryModel::PackEditorInventoryModel(QObject* parent) : QAbstractTableModel(parent) {}

int PackEditorInventoryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_entries.size(); }
int PackEditorInventoryModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant PackEditorInventoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) return {};
    const auto& entry = m_entries.at(index.row());
    if (role == EntryRole) return entry;
    if (role == IdentityRole) return identity(entry);
    if (role == TargetsRole) return entry.value("targets").toArray();
    if (role == StateRole) return entry.value("status").toString();
    if (role == SearchTextRole) {
        QStringList values;
        for (const auto& key : {QStringLiteral("name"), QStringLiteral("filename"), QStringLiteral("provider"),
                                QStringLiteral("source"), QStringLiteral("id"), QStringLiteral("identity"), QStringLiteral("project_id")})
            values.append(entry.value(key).toString());
        return values.join(QLatin1Char(' '));
    }
    if (role == Qt::DecorationRole && index.column() == NameColumn)
        return m_icons.value(identity(entry));
    if (role != Qt::DisplayRole) return {};
    switch (index.column()) {
        case NameColumn: {
            const QString name = entry.value("name").toString().trimmed();
            if (!name.isEmpty() && name != entry.value("id").toString()) return name;
            const QString filename = entry.value("filename").toString();
            return filename.isEmpty() ? tr("Name unavailable") : filename;
        }
        case VersionColumn: return entry.value("version").toString().isEmpty() ? tr("—") : entry.value("version").toString();
        case ProviderColumn: {
            const auto source = entry.value("provider").toString(entry.value("source").toString());
            return source.isEmpty() ? tr("Unknown source") : source;
        }
        case TargetsColumn: {
            if (!entry.value("targets").isArray()) return tr("Client (legacy default)");
            const auto targets = entry.value("targets").toArray();
            QStringList labels;
            if (targets.contains(QStringLiteral("client"))) labels.append(tr("Client"));
            if (targets.contains(QStringLiteral("server"))) labels.append(tr("Server"));
            if (labels.isEmpty()) return tr("On this computer only");
            return labels.join(QStringLiteral(" · "));
        }
        case StateColumn: {
            const QString state = entry.value("status").toString();
            if (state == QStringLiteral("synced")) return tr("Included");
            if (state == QStringLiteral("modified")) return tr("Changed here");
            if (state == QStringLiteral("missing")) return tr("Missing locally");
            if (state == QStringLiteral("ignored")) return tr("Excluded");
            if (state == QStringLiteral("unmanaged")) return tr("Unmanaged");
            if (state == QStringLiteral("conflict")) return tr("Needs review");
            return state.isEmpty() ? tr("Unknown") : state;
        }
    }
    return {};
}

QVariant PackEditorInventoryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case NameColumn: return tr("Mod");
        case VersionColumn: return tr("Version");
        case ProviderColumn: return tr("Source");
        case TargetsColumn: return tr("Included on");
        case StateColumn: return tr("State");
        default: return {};
    }
}

void PackEditorInventoryModel::setEntries(const QJsonArray& entries)
{
    beginResetModel();
    m_entries.clear();
    m_entries.reserve(entries.size());
    QHash<QString, int> rowsByIdentity;
    const auto statusRank = [](const QString& status) {
        if (status == QStringLiteral("conflict")) return 6;
        if (status == QStringLiteral("missing")) return 5;
        if (status == QStringLiteral("modified")) return 4;
        if (status == QStringLiteral("ignored")) return 3;
        if (status == QStringLiteral("unmanaged")) return 2;
        return 1;
    };
    for (const auto& value : entries) {
        if (!value.isObject()) continue;
        const auto incoming = value.toObject();
        const QString resourceIdentity = identity(incoming);
        const QString key = resourceIdentity + QLatin1Char('\0') + incoming.value("filename").toString();
        if (resourceIdentity.isEmpty() || !rowsByIdentity.contains(key)) {
            rowsByIdentity.insert(key, m_entries.size());
            m_entries.append(incoming);
            continue;
        }
        auto merged = m_entries.at(rowsByIdentity.value(key));
        auto states = merged.value("target_states").toArray();
        QSet<QString> stateTargets;
        for (const auto& state : states) stateTargets.insert(state.toObject().value("target_id").toString());
        for (const auto& state : incoming.value("target_states").toArray()) {
            const auto target = state.toObject().value("target_id").toString();
            if (!stateTargets.contains(target)) states.append(state);
        }
        merged.insert("target_states", states);
        if (incoming.value("managed").toBool() && !merged.value("managed").toBool()) {
            merged.insert("managed", true);
            merged.insert("targets", incoming.value("targets"));
        }
        if (statusRank(incoming.value("status").toString()) > statusRank(merged.value("status").toString()))
            merged.insert("status", incoming.value("status"));
        m_entries[rowsByIdentity.value(key)] = merged;
    }
    endResetModel();
}

void PackEditorInventoryModel::setIcon(const QString& key, const QIcon& icon)
{
    if (key.isEmpty() || icon.isNull()) return;
    m_icons.insert(key, icon);
    for (int row = 0; row < m_entries.size(); ++row) {
        if (identity(m_entries.at(row)) == key) emit dataChanged(index(row, NameColumn), index(row, NameColumn), {Qt::DecorationRole});
    }
}

QJsonObject PackEditorInventoryModel::entryAt(int row) const { return row >= 0 && row < m_entries.size() ? m_entries.at(row) : QJsonObject(); }
int PackEditorInventoryModel::rowForIdentity(const QString& key) const
{
    for (int row = 0; row < m_entries.size(); ++row) if (identity(m_entries.at(row)) == key) return row;
    return -1;
}
QString PackEditorInventoryModel::identity(const QJsonObject& entry) const
{
    for (const auto& key : {QStringLiteral("identity"), QStringLiteral("id"), QStringLiteral("filename")}) {
        const auto value = entry.value(key).toString();
        if (!value.isEmpty()) return value;
    }
    return {};
}

PackEditorInventoryFilter::PackEditorInventoryFilter(QObject* parent) : QSortFilterProxyModel(parent) { setDynamicSortFilter(true); }
void PackEditorInventoryFilter::setSearchText(QString text) { m_search = std::move(text); invalidateFilter(); }
void PackEditorInventoryFilter::setTargetFilter(QString target) { m_target = std::move(target); invalidateFilter(); }
void PackEditorInventoryFilter::setStateFilter(QString state) { m_state = std::move(state); invalidateFilter(); }

bool PackEditorInventoryFilter::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    const auto source = sourceModel()->index(row, 0, parent);
    const auto entry = source.data(PackEditorInventoryModel::EntryRole).toJsonObject();
    if (!m_search.trimmed().isEmpty() && !source.data(PackEditorInventoryModel::SearchTextRole).toString().contains(m_search.trimmed(), Qt::CaseInsensitive)) return false;
    const auto targetsValue = entry.value("targets");
    const auto targets = targetsValue.toArray();
    const bool client = !targetsValue.isArray() || targets.contains(QStringLiteral("client"));
    const bool server = targets.contains(QStringLiteral("server"));
    if (m_target == QStringLiteral("client") && !client) return false;
    if (m_target == QStringLiteral("server") && !server) return false;
    if (m_target == QStringLiteral("shared") && !(client && server)) return false;
    const auto state = entry.value("status").toString();
    if (m_state == QStringLiteral("changed") && state != QStringLiteral("modified") && state != QStringLiteral("missing") && state != QStringLiteral("conflict")) return false;
    if (m_state == QStringLiteral("excluded") && state != QStringLiteral("ignored")) return false;
    return true;
}

bool PackEditorInventoryFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator.compare(left.data(Qt::DisplayRole).toString(), right.data(Qt::DisplayRole).toString()) < 0;
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
        for (const auto& key : {QStringLiteral("name"), QStringLiteral("filename"), QStringLiteral("id"), QStringLiteral("path")}) {
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
