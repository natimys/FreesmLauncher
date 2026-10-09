# ModLock UI design prototype

This is a standalone, interactive browser prototype for design review. It is not part of FreesmLauncher runtime and uses clearly identified mock data. No production UI or ModLock backend is included.

## Open

Open `index.html` in a modern desktop browser. No package install, web server, or network connection is needed. The left rail depicts simulated FreesmLauncher navigation and proposed instance pages. Use the separate **Prototype screen** selector in the top bar to jump between review screens; it is a demo-only shortcut and does not represent production navigation. Production navigation is described in the design documents.

The mod inventory preserves query, filters, selection, sort, and scroll when selecting another row. Target checkboxes reflect the selected mod’s actual mock membership and require Apply. Search/filter/sort, add-one-mod with target selection, target edits, managed-file additions, publication success/partial failure/pre-commit failure, update success, all-or-defer conflict outcomes, player states, and import success/errors use local mock state.

## Limitations

- The pack summary says 326 mods, while the prototype includes only 20 representative mod rows. It is not a 326-row performance test.
- Provider results, publication, install, conflict resolution, and status transitions are simulated. Data is in-memory, disappears on reload, and never writes files or connects to repositories/ModLock.
- Managed files use a separate mock collection and Files screen; they are not represented as mod resources.
- The sample palette is intentionally fixed dark styling. It does not preview Freesm system/light/Gruvbox/custom themes or high-DPI Qt rendering.
- The prototype screen selector exposes reviewer-only workflow routes that production reaches contextually; it is not a production navigation mock.
- This is a browser artifact mapped to Qt Widgets, not a runnable Qt app. A captured screenshot is not included.

## Qt mapping

Proposed production instance pages map to `InstancePageProvider`/`PageContainer`; the prototype screen selector has no production counterpart. Inventory/search/detail maps to `QListView`/`QTableView`, `QSortFilterProxyModel`, `QSplitter`, and a detail widget. Controls map to `QPushButton`, `QToolButton`, `QCheckBox`, `QMenu`, and `QDialog`. Status uses theme icons and `QPalette`; lists use `QAbstractItemModel`; progress uses `ProgressDialog`/`InstanceTask`. Every production state must come from `ModLockBridge` payloads, never the mock logic here.
