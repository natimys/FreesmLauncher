# ModLock implementation handoff

The proposal changes UI only. Implement in screen-sized reviewable steps and preserve bridge semantics. Paths below are relative to FreesmLauncher root.

## P0 — Player state, update review, conflict, publish truth

**Deliver:** concise Modpack Overview; structured update review; consequence-first conflict dialog; structured author review/results. **Files:** `launcher/ui/pages/instance/ManagedPackPage.{h,cpp,ui}`, `launcher/launch/steps/ModLockUpdate.cpp`, `launcher/modlock/ModLockConflictDialog.{h,cpp}`, `launcher/ui/pages/instance/PackEditorPage.{h,cpp}`, new Qt review dialog/model. **Reuse:** `ModLockBridge`, `ProgressDialog`, `InstanceTask`, current preview/conflict payloads. **Dependency:** if author preview lacks typed fields, add Go bridge fields; never parse display prose. **Acceptance:** one global Play; truthful offline states; reviewed exact revision preserved; only confirm-all/defer-all conflicts; stale conflict asks again; push failure says “Saved locally; remote not updated.”

## P1 — Unified author inventory and file language

**Deliver:** unified mod model, target/state proxies, detail pane, target checkboxes, contextual actions, human Files/Excluded terms. **Files:** `launcher/ui/pages/instance/PackEditorPage.{h,cpp}`, `launcher/modlock/PackEditorModel.{h,cpp}`, optional delegate/model, provider integration in `launcher/ui/pages/modplatform/ResourcePage.*` and `ModrinthPage.*`. **Reuse:** author state, target operation, provider metadata/icons, Qt model/view. **Dependency:** confirm actual inverse/exclude and add/update scope before exposing; no atomic bulk claims. **Acceptance:** shared mod once; two explicit targets; empty membership cannot save; 300–500 entries search/sort; keyboard selection works; hashes hidden by default; file membership versus disk action is explicit.

## P1 — Import onboarding

**Deliver:** URL-first form, collapsed branch/path, stable preview, actionable errors. **Files:** `launcher/ui/pages/modplatform/ModLockImportPage.{h,cpp}`, `launcher/ui/dialogs/NewInstanceDialog.cpp`, `launcher/modlock/ModLockCreationTask.cpp` if task wiring requires. **Reuse:** `read` preview, exact profile resolution, shared New Instance fields, staged task/conflict dialog. **Dependency:** none for layout; use current capabilities. **Acceptance:** one prominent URL, advanced defaults persist, edits invalidate preview, Schema 1 manual components remain, Schema 2/3 exact profile, failed/cancelled stage never registered.

## P2 — Author navigation/workspace setup

**Deliver:** show editor only for author workspace, separate player/author modes, explain supported setup/promotion. **Files:** `launcher/InstancePageProvider.h`, `launcher/ui/pages/instance/PackEditorPage.cpp`, instance page routing, workspace helper/model. **Reuse:** `BasePage::shouldDisplay`, author settings detection and existing promotion operation. **Dependency:** no generic create action unless safe operation exists. **Acceptance:** ordinary player instances do not show disabled author console; promotion is explicit and describes source/destination/backup; schema does not migrate implicitly.

## Shared requirements and visual review

- Reuse `ThemeManager`, `QPalette`, platform metrics and theme icons; no fixed semantic RGB.
- Translate strings; verify keyboard/focus/accessibility and high DPI.
- Trust launcher-provided filesystem roots, never lock paths as root selection.
- Keep protocol parser, typed errors, stderr diagnostics and cooperative cancellation intact.
- `verify` is read-only/offline; hashless data remains unverified as appropriate.
- Preserve exact preview revision, transactional apply/rollback, local-file hash confirmation, recovery location, push failure distinction.
- Capture 1440×900, 1024×700 and narrow 800 px screenshots under light, dark and one custom theme. Review no workspace, empty/filter states, long strings, missing metadata, 500 mods, shared targets, dirty/stale review, push failure, offline verified/unverified, conflict/defer, pending cancel and recovery failure.
- Check 100/150/200% DPI, native focus/tab order, accessible names, contrast, filter selection identity, target serialization and exact revision propagation.

## Delivery sequence

1. Player/update/conflict/review truth. 2. Unified editor and file terms. 3. Provider integration and target semantics. 4. Import onboarding and schema paths. 5. Workspace navigation/setup. Keep each step independently reviewable. The prototype is mock-only, not wired to ModLock.
