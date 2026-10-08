// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QIcon>
#include <QHash>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>

struct PackEditorAuthorState {
    QJsonValue lock;
    QJsonObject settings;
    QJsonArray mods;
    QJsonArray files;
    QJsonArray trackedPaths;
    QString branch;
    bool dirty = false;

    static bool parse(const QJsonObject& result, PackEditorAuthorState* state, QString* error = nullptr);
};

// Compact per-target status text for details/table views. Falls back to the
// aggregate status used by older Core bridge responses.
QString packEditorStatusSummary(const QJsonObject& entry);
bool packEditorPageShouldDisplay(bool isMinecraftInstance);
bool modLockImportSchemaSupported(int schema);
QJsonObject packEditorSetModTargetsParams(const QString& id, const QJsonArray& targets);

// A target-specific projection over one authoritative author-state list. A
// shared entry stays singular in bridge state and is exposed by both models.
class PackEditorTargetModel final : public QAbstractListModel {
   public:
    enum Role { EntryRole = Qt::UserRole + 1, IdentityRole, SharedRole };

    explicit PackEditorTargetModel(QString target, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(const QJsonArray& entries);
    void setShowShared(bool showShared);
    void setIcon(const QString& identity, const QIcon& icon);
    QString target() const { return m_target; }
    QJsonObject entryAt(int row) const;

   private:
    bool entryMatches(const QJsonObject& entry) const;
    bool isShared(const QJsonObject& entry) const;
    QString identity(const QJsonObject& entry) const;
    void rebuildVisibleEntries();

    QString m_target;
    QJsonArray m_entries;
    QVector<QJsonObject> m_visibleEntries;
    QHash<QString, QIcon> m_icons;
    bool m_showShared = true;
};
