# ModLock UI design prototype

This is a standalone, interactive browser prototype for design review. It is not part of FreesmLauncher runtime and uses clearly identified mock data. No production UI or ModLock backend is included.

## Open

Open `index.html` in a modern desktop browser. No package install, web server, or network access is needed. Use the left rail to visit Pack Editor, Mod details, Files, Review & Publish, Modpack Overview, Update preview, Conflict, and Import. The editor search/filters and navigation, target checkboxes, overview state selector, review publish status, conflict choices, and import preview are interactive.

Controls demonstrate proposed states only. They do not write files, access a repository, or invoke bridge operations. Mock data: Clockwork Valley pack, version 1.8.1, 326 mods (20 sample rows), 34 files, 6 excluded items, and fabricated update/review outcomes. The prototype uses an intentionally fixed dark sample palette; production maps semantic roles to the selected Freesm Qt theme.

## Qt mapping

The rail maps to `InstancePageProvider`/`PageContainer`. Inventory/search/detail maps to `QListView`/`QTableView`, `QSortFilterProxyModel`, `QSplitter`, and a detail widget. Controls map to `QPushButton`, `QToolButton`, `QCheckBox`, `QMenu`, and `QDialog`. Status uses theme icons and `QPalette`; lists use `QAbstractItemModel`; progress uses `ProgressDialog`/`InstanceTask`. Every production state must come from `ModLockBridge` payloads, never the mock logic here.
