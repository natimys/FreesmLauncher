# FreesmLauncher + ModLock visual design

**Status:** implementation-ready design proposal. **Scope:** ModLock screens only; Qt Widgets and the existing bridge remain in place.

## Direction and evidence

Extend FreesmLauncher’s existing instance pages, provider browser, native dialogs, icon themes and user-selected widget themes. Use a compact identity header, one searchable inventory, and a persistent contextual detail pane. Whitespace and alignment establish hierarchy; avoid a separate brand, dashboard cards, decorative gradients, and a wall of framed panels.

Source inspection and the existing redesign audit show a Qt Widgets application with `ThemeManager` support for system, Freesm, Freesm Light, Bright, Gruvbox and custom themes. The provider browser already pairs search/filter controls with a `QSplitter` and resource description. Current Pack Editor has four tabs, duplicated client/server lists, six same-weight actions, technical columns and a text-based publish preview. Import shows URL, branch and lock path together. These are source findings; no live visual validation is claimed.

The bridge remains authoritative. Preserve Schema 1/2/3; Schema 1 needs manual profile selection, Schema 2/3 resolve exact supported components, and Schema 3 stores explicit logical `client` and `server` target IDs. Shared means both IDs are selected. `check` returns an exact revision; `apply` must use it. `verify` is offline/read-only. Apply is transactional, supports hash-bound conflict confirmation and may report recovery failure. Author publish can commit locally and fail to push. Cancellation is cooperative. UI language must not imply weaker or stronger guarantees.

## Information architecture

**Player:** New Instance → ModLock source → preview → create; then the ordinary instance selection and global Play. A managed instance has a concise **Modpack** page for identity, installed pack version, Minecraft/loader, source, update status, health and last known check. It does not add another Play button.

**Maintainer:** expose **Pack Editor** for a valid author workspace or through an explicit supported setup route. Do not insert a disabled author console into ordinary instance settings. Editor sections are **Mods**, **Files**, and **Excluded**. Pack identity/workspace state remains in a compact header. **Review changes** opens a structured review; it is the sole publication entry point.

## Pack Editor geometry

Reference size 1440×900 logical pixels; preserve usability at 1024×700 and reflow at 800 px wide. Use a 24 px page inset (minimum 16), 20 px section separation, and 8–16 px control groups. Respect Qt layout/style metrics and device-independent sizing.

1. **Identity header, 64–80 px:** pack name; secondary line for pack version and game profile; quiet workspace/branch status. Refresh and More remain secondary. Never headline a path or commit hash.
2. **Navigation, 40 px:** Mods / Files / Excluded as compact tabs consistent with the launcher.
3. **Toolbar, 44 px:** search first; target filter (All, Client, Server, Shared), state filter (All, Changed, Excluded), sort, and overflow for rare filters. Add mod is primary while browsing; Review changes becomes the only filled primary when dirty.
4. **Inventory and details:** horizontal `QSplitter`, default 68/32. Inventory minimum 390 px, detail minimum 280 px. One-line count/active filter summary above rows. Detail stays open with selected row; at compact width collapse to a side sheet/dialog.
5. **Change footer, 44–52 px:** pinned only while dirty, e.g. “4 local changes · 2 mods · 2 files” and Review changes. No permanent commit field or Publish button.

### Unified inventory

One row per stable mod identity, including shared resources only once. 56–64 px row height; 28–32 px icon, readable name, version/provider line, target text badges, compact management/local state. Missing metadata becomes “Name unavailable” plus known filename/provider, never an internal ID headline.

Quick target filters: All, Client, Server, Shared. Quick state filters: All, Changed, Excluded. Provider/source/unverified-metadata filters live in a menu. Search matches name, filename, provider and stable ID; IDs are secondary. Sort by name, target, version, provider, or state. Preserve selection/scroll through filtering and async metadata. Use `QSortFilterProxyModel`, uniform rows and virtualized model/view for 300–500+ entries; do not build cards per mod.

Keyboard: Ctrl+F or `/` focuses search; Up/Down selects; Space opens details; Enter activates the primary item action; Delete opens consequence-specific confirmation; Escape clears search then selection; context menus are keyboard-accessible and list shortcuts. Bulk selection/actions are offered only where the actual operation supports the scope and partial outcomes are reported honestly.

Preserve search text, target/state filters, selected resource identity, sort order, and list scroll offset as one editor view state. Selecting another row updates selection/details in place; it must not rebuild the inventory or reset scroll. Sorting may rebuild rows, but restores the same query, filters, selection, and a stable scroll position. If filtering hides the selected row, retain its details and identify it as outside the visible results.

### Details and target editing

Hierarchy: icon/name; installed version/provider; target assignment; pack/local state; warnings; valid actions; collapsed **Technical details**. Show readable source, filename, target presence, and verified/unverified only when available. IDs, URL, full digest, schema and revision are copyable technical detail.

Primary target editor: two labeled checkboxes **Client** and **Server**, with an explicit **Apply targets** button after edits. This directly maps to two logical IDs and supports client-only, server-only or shared without inventing a `both` enum. Never save an empty target set; disable Apply with explanation. Accessible labels name resource and target (e.g. “Include Sodium on Server”). Focus stays in details after save; row badges update. Legacy no-target entries read “Client (legacy default)” until explicitly promoted.

At compact widths, selecting a row opens the detail side sheet. Keep a persistent **Selected details** control near the inventory toolbar so details remain discoverable after resizing. Closing the sheet returns focus to the selected row.

Actions depend on state: Choose version/Update, Change targets, Exclude from pack, Stop managing, Remove from pack. Less common actions sit in More. Explain consequences: excluding changes publication intent; stopping management leaves local bytes unmanaged; removal can remove managed content on the next sync. Do not label each action “Remove.”

## Files, tracking, and states

**Files** uses a compact `QTableView`: Path, Included on, Update behavior, Status. Search is path-first; selection shows origin, destination, target roots, policy and state in details. Translate backend values: `replace` → **Update from pack** (“may replace a local edit after review”); `if_missing` → **Install when missing** (“keeps an existing regular file”). “Tracked paths” becomes a collapsed **Watch for files** section. “Ignored” becomes **Excluded from this pack**. “Unmanaged” becomes **On this computer only**, with a clear statement about publication/update effects when supported.

State labels pair theme icon and text: Included, Changed here, Missing, Excluded, On this computer only, Needs review, Verified, Unverified. Local edit does not itself authorize replacement. Pack-membership edits and local disk writes are separate operations; action copy names which one occurs. Full SHA-256/source path stay in technical details.

## Review and publish

Obtain an authoritative preview and keep its opaque token/snapshot attached to what is rendered. Any edit/refresh invalidates it and requires a new preview. Group returned entries under Added, Updated, Removed, Files, and Excluded/not published; each group has count, target impact, and expand/collapse. Show returned validation warnings before the list. Search/filter remains available for large reviews; counts stay visible when collapsed. Do not infer facts missing from the bridge payload.

Commit message: suggested one-line summary plus optional disclosed multi-line detail. Final action reads “Publish 3 changes,” disabled until preview is fresh/valid. Confirmation explains local commit and push to configured remote. Progress steps: Preparing preview → Creating local commit → Sending to remote. Cancel stays pending until the bridge terminates. Success reads **Published** only after remote push succeeds. Partial failure reads **Saved locally; remote not updated**, with branch/commit in secondary diagnostics. Offer Retry push only if an existing backend operation supports it. Cancellation and recovery failure are separate states.

## Player overview and updates

Overview uses normal instance page width, not a dashboard. Show pack name/icon and installed version, one line for game+loader, source under secondary detail, then one status row, last-check information and next action. Global Play remains unique. Ready state is quiet; Update available promotes **Review update**. Status copy:

- Ready/up to date: say verified only after verification.
- Checking: “Checking for pack changes…”
- Update available: destination version and View changes.
- Applying: “Installing this checked build…”
- Local files modified: “Some pack files were edited here. Review before replacing them.”
- Recovery required: update could not finish safely; retain instance and show next support/recovery step.
- Offline verified: source unavailable; installed files passed available offline checks. Do not imply freshness.
- Offline unverified/incomplete: name uncertainty; block launch where current behavior blocks.
- Update failed: cause and only supported retry/back action.

Update review shows From→To versions/profile and structured additions, updates, removals, managed files, excluded items, explicit Client/Server impact and local changes. Lists can collapse; counts remain visible. Apply action says **Install this update** and explains exact reviewed revision plus a second local-file check. Never switch silently to a moving branch tip.

## Conflict and offline dialog

Title: **Some local files would change**. Lead with consequences, affected count, readable path, and Replace/Delete. Keep hashes under Technical details. Default action: **Keep my local files and defer this update**. Alternative: **Replace these files and continue** for the whole shown conflict set. No per-file mixed choices. Copy explains that keep/defer defers the whole update. Hash confirmation is checked again; stale result reopens with fresh conflicts. Cancel/defer does not apply pack changes.

Offline offers retry, return/cancel, or launch installed build only when `verify` allows it. Distinguish verified/unverified/incomplete. Recovery failure blocks launch and retains staging. Cancellation waits for bridge terminal completion; do not claim stopped while applying/rolling back.

## Import

Keep existing New Instance/provider browser. First/focused input is **Repository URL**; paste/drop supported Git URL. Branch (`main`) and lock path (`mod.lock`) are under **Advanced options**. Preview is explicit; typing does not fetch. Validate format inline without claiming reachability. Show accessible progress and cooperative Cancel.

Successful preview shows pack identity/version, Minecraft+loader when available, mod/file counts when returned, source branch, and “This creates a new instance; the current library is unchanged.” Schema 1 includes existing manual component selection with a brief reason; Schema 2/3 show exact resolved profile. Editing source/branch/path invalidates preview. Errors cover unsupported schema, unavailable game/loader, missing lock, network/auth, malformed lock with an actionable correction; inputs remain intact. Do not make Git expertise a prerequisite.

## Reusable design system

### Type and spacing

Use current launcher/platform font. Page title 18–22 px at 100% scale, medium/semibold; section 14–16 px semibold; body/row native 13–14 px; secondary metadata native size and never below 11 px; technical values platform monospace at native size. Scale with Qt device-independent pixels. Insets 24 px (minimum 16); compact gap 8; field/row padding 8–12; section gap 20–24; dialog inset 24; inventory rows 56–64; table rows 36–44. Respect `PM_Layout*Margin` and font metrics.

### Color and semantic roles

Resolve semantic roles from current `QPalette` and theme APIs: Window/Base, AlternateBase, Text, PlaceholderText, Highlight/HighlightedText, Button/ButtonText, Link, ToolTip, disabled text. Primary uses app accent/highlight; secondary uses native button; destructive action uses existing negative/status icon treatment plus text, never fixed red. Success/warning/error/info use theme palette/icons. Selection combines highlight and focus outline. Avoid fixed RGB and forced widget-specific color. Check all built-in and custom themes.

### Components and states

| Component | Default / interaction | States and accessibility |
|---|---|---|
| Inventory row | Icon, name, version/provider, target badges, one state; single selection/context menu | Hover, focus, selected, changed, excluded, missing, metadata unavailable; badges have text |
| Target indicator/editor | Client and Server text; two checkboxes in details | Accessible target names; never color-only; nonempty selection required |
| Search/filter toolbar | Search, target/state menu, sort | Keyboard clear/focus; announced active-filter summary |
| Detail pane | Selected facts, primary/secondary actions, technical disclosure | Empty prompt; compact side sheet; focus returns to row |
| Empty state | Plain reason and one relevant action | No mods/files, no results, no workspace distinguishable |
| Status | Theme icon + label + short explanation | Ready/checking/warning/error/offline; color-independent |
| Change summary | Counts by operation/target, leads to review | No changes, stale/invalid preview visible |
| Progress | Existing task/progress component and supported Cancel | Indeterminate, cancel pending, success, failure, recovery |
| Inline error | Specific sentence and valid retry/edit action | Validation/network/auth/schema/stale/recovery |
| Confirmation | Consequence title, affected count/path, safe default, explicit verb | Keyboard-safe default and distinct destructive action |
| Technical disclosure | IDs, hashes, exact paths, schema/revision; copyable | Collapsed; selectable/wrapped; no tooltip-only information |
| Action group | One filled primary; quiet secondary; destructive in More | Disabled reason and logical tab order |

## Accessibility, DPI, and responsive behavior

Make all workflows keyboard-complete with native focus rings and stable focus after async completion. Give icon-only controls accessible names; announce progress/status; expose row targets/state to assistive technology. Respect text scaling and localization: wrap toolbar, wrap explanations, elide only secondary metadata; avoid fixed window sizes. Below 800 px move secondary filters to a Filter button and details to a side dialog; at 800–1024 let toolbar wrap and collapse pane. Use vector/theme-aware or correctly DPR-scaled icons. Review light, dark, custom themes, contrast, color-vision distinctions, and 100/150/200% DPI.

## Prototype navigation and scope

The prototype’s **Prototype screen** selector is a reviewer-only route switcher. It is visually separate from simulated Freesm launcher chrome and must not be copied into production. Production navigation remains the instance page hierarchy: Modpack for managed players, Pack Editor when an author workspace exists, and Files/Excluded as editor sections. Review, Update preview, Conflict, and Import are workflow screens/dialogs reached from their entry actions, not permanent instance-page destinations. Prototype mock data keeps mod resources and managed files in separate collections and screens.

## Qt implementation mapping

| Design area | Existing anchor | Implementation reality |
|---|---|---|
| Navigation/mode | `InstancePageProvider`, `BasePage::shouldDisplay`, `InstanceWindow`/`PageContainer` | UI refactor; gate author editor by workspace, retain managed player surface |
| Provider search | `ModrinthPage.ui/.cpp`, `ResourcePage`, provider indexes, `ResourceDownloadDialog` | Reuse browser; preserve selected resource/version and ask targets once per supported operation |
| Inventory/details | `PackEditorPage`, `PackEditorModel`, `QSplitter`, `QSortFilterProxyModel`, delegate | New unified model/proxy from one author state list |
| Target changes | Existing Pack Editor target operation/model roles, `ModLockBridge` | UI-level checkboxes; explicit `client`/`server`; no literal both |
| Files/excluded | `PackEditorTableModel`, author-state mods/files/trackedPaths | UI terminology/layout refactor; show only supported inverse actions |
| Structured review | Pack Editor preview/publish handlers and returned payload | UI refactor if typed categories exist; otherwise add typed Go fields, never parse prose |
| Player overview/update | `ManagedPackPage`, `MinecraftInstance`, `ModLockUpdate`, `ModLockBridge` | UI refactor over existing read/check/verify/apply states |
| Conflict | `ModLockConflictDialog`, install/update tasks | UI refactor; backend supports confirm-all or defer-all only |
| Import | `ModLockImportPage`, `NewInstanceDialog`, `ModLockCreationTask` | UI refactor; preserve schema behavior and staged registration |
| Progress/recovery | `ProgressDialog`, `InstanceTask`, bridge signals/error codes | UI refactor; bridge owns cancellation/terminal/recovery truth |

Do not assume arbitrary bulk edits, per-file conflict choices, retry-push, generic author initialization, or extra verification facts. Preserve trusted launcher filesystem roots, schema compatibility, exact-revision updates, transactional rollback/recovery, conflict protection, offline verification limits, and cooperative cancellation.
