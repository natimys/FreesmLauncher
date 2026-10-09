# ModLock screen specifications

Production behavior proposal. User-facing strings are suggestions and must be translatable. Shared layout and components are specified in [visual design](modlock-visual-design.md).

## 1. Pack Editor — Mods

Entry is an explicit Pack Editor destination for a valid author workspace. Header shows pack and workspace state. Section tabs are Mods, Files, Excluded. Toolbar has search, target filter All/Client/Server/Shared, state filter All/Changed/Excluded, sort, and advanced-filter menu. One unified row per stable resource; shared appears once. Selecting a row fills details without changing list scroll/selection.

Add mod opens the existing provider browser in context. Add mod is primary when inventory is clean; Review changes is the only filled primary when dirty. Row actions are Choose version/Update, Change targets, Exclude from pack, Stop managing, Remove from pack; consequences distinguish published membership from disk changes. Do not imply atomic bulk operations.

States: loading, ready, dirty, search-empty, no mods, missing metadata, workspace unavailable, refresh/scan, bridge failure. A workspace absence explains player/author mode and offers only a capability-backed setup action. No disabled console full of controls.

## 2. Mod details and target editing

Show identity, installed version/provider, Client and Server checkboxes, pack/local state, warning, valid actions, then collapsed technical detail. Save a nonempty target set only. Legacy missing target reads “Client (legacy default)” until explicitly saved. Keep focus after save and update row badges. Missing provider info reads “Name unavailable” with known filename/provider; async lookup must not clear selected resource.

## 3. Files and Excluded

Files is a compact table: Path / Included on / Update behavior / Status. Search path-first. Selection shows source, destination, policy and target roots. Use “Update from pack” and “Install when missing” with concise help. Watch for files is a collapsed rule group. Excluded has searchable resources and a reversible Include again only if backend exposes a safe inverse. On this computer only describes unmanaged resources. Separate pack-definition actions from local file writes.

## 4. Review changes and publish

Review obtains/uses current authoritative preview. Any intervening edit invalidates it and disables Publish. Summary shows count, target impact, warnings and grouped Added/Updated/Removed/Files/Excluded changes. Groups expand/collapse and support search; counts remain visible. Show only data actually returned; do not infer impacts.

Use suggested one-line summary and optional disclosed description. Explicit “Publish N changes” confirmation explains local commit and remote push. Progress has preview, local commit, push phases; Cancel is cooperative until terminal event. Results distinguish Published, Saved locally/remote not updated, pre-commit failure, cancellation and recovery/diagnostic. Retry is offered only if backend supports it.

## 5. Modpack Overview

Show pack name/version, Minecraft/loader, source, update status, installation health, and last known check. The launcher’s global Play remains the only Play action. Ready claims file verification only after verify; checking shows progress; update available shows destination version and View changes; applying says it is installing checked build; local modifications warn before replacement; recovery retains instance and blocks launch when current behavior blocks; offline verified reports only available offline checks; offline unverified/incomplete names uncertainty; update failure offers only supported retry/back.

No author workspace: retain player overview and hide author tools; expose a setup/migration action only if supported.

## 6. Update review

Header: installed→checked version and profile. List added/updated/removed mods and managed files, excluded resources, client/server impact and local changes. Missing metadata is identified. Expandable groups retain counts. Continue says “Install this update,” explains exact reviewed revision and recheck of local files, then routes conflicts to common dialog. Apply receives exact revision; never update to branch tip implicitly.

## 7. Conflict dialog

“Some local files would change.” Explain affected count, readable paths and Replace/Delete outcome. Hashes are technical details. Supported choices are “Keep my local files and defer this update” (safe default), “Replace these files and continue” for all shown conflicts, and Cancel. No per-file mix. Apply binds observed hashes and stale confirmation returns updated conflicts; reopen review with fresh list. Recovery failure is not ordinary cancellation.

## 8. Import ModLock pack

Initial New Instance source form has one prominent Repository URL and explicit Preview. Branch (`main`) and lock path (`mod.lock`) are collapsed advanced options. No automatic fetch while typing. Malformed URL is inline; loading is cancellable cooperatively; edits invalidate preview.

Successful preview shows identity/version, game/loader, counts if returned, source branch and “This creates a new instance; the current library is unchanged.” Schema 1 retains existing manual component selection and explains why; Schema 2/3 show exact resolved profile. Create uses existing staged install and registers only on success.

Errors distinguish unsupported schema, unavailable profile component, missing lock, network/authentication, and malformed lock, with a next action and preserved input. Import must not require Git vocabulary.

## Shared navigation, progress, empty states

Zero search results offers Clear filters. Empty inventory/files/excluded distinguish none, not loaded, and no workspace. Progress stays until terminal bridge event; Cancel means request sent, not stopped. Retry appears only for supported operations. Dialogs return focus to initiating control; keyboard navigation and accessible announcements follow component system.
