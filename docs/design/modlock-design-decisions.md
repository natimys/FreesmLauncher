# ModLock design decision log

## D1 — Unified inventory over duplicated target lists

**Alternatives:** duplicate client/server columns; unified rows with labels; table-only. **Chosen:** one searchable identity row with target labels and optional later Compare targets view. **Reason:** shared resources currently duplicate, split scroll and reduce width; unified model scales and preserves selection. **Cost:** author must read badges instead of spatial columns; explicit labels/filters compensate. Requires authoritative one-list model and proxy. Comparison can be derived later.

## D2 — Persistent detail pane over modal details

**Alternatives:** modal per resource; details below list; `QSplitter` right pane. **Chosen:** persistent pane, side sheet/dialog at compact width. **Reason:** edit while retaining list position and follows provider browser’s split pattern. **Cost:** splitter/selection state and narrow mode; modal is simpler but interrupts curation.

## D3 — Two target checkboxes over segmented Shared choice

**Alternatives:** Client/Server/Shared selector; two checkboxes; context menu. **Chosen:** Client and Server checkboxes plus Apply targets. **Reason:** directly maps explicit Schema 3 IDs and does not invent a `both` enum; state is visible. **Cost:** one deliberate Apply step; provider-side classification can seed but never override author choice. Empty set is disabled.

## D4 — Search plus high-frequency filters

**Alternatives:** all chips always visible; all filters in modal; search plus target/state/sort controls and advanced menu. **Chosen:** latter. **Reason:** client/server/shared and changed remain one click away without consuming toolbar at small widths. **Cost:** rare filters less discoverable; active filter summary helps.

## D5 — Structured review then publish

**Alternatives:** permanent commit form/button; plain-text preview; dedicated structured review. **Chosen:** edit → authoritative preview → review → message → explicit publish. **Reason:** text hides target/category impact; separate review is inspectable with hundreds of entries and stale token handling. **Cost:** one extra step and typed preview model; if payload lacks fields, add bridge fields rather than parse prose.

## D6 — Player overview without duplicate Play

**Alternatives:** another Play button; multi-card dashboard; concise state page plus global Play. **Chosen:** concise status page and existing global action. **Reason:** one launch/update path avoids ambiguity. **Cost:** a page-only view has no local Play control, but launcher chrome remains persistent.

## D7 — Safe whole-update conflict default

**Alternatives:** default replace; per-file checkboxes; confirm all or defer all. **Chosen:** keep local and defer whole update by default; alternate confirms all conflicts. **Reason:** backend supports these choices and destructive ambiguity should preserve local content. **Cost:** cannot apply unaffected subset; per-file mixed decision would misrepresent behavior.

## D8 — Human file language with technical disclosure

**Alternatives:** expose IDs/hash/policy columns; hide technical data entirely; human terms first with disclosure. **Chosen:** human status and update behavior in row/detail, copyable paths/IDs/digests in Technical details. **Reason:** warn about replacement while keeping routine work readable and retaining diagnostics. **Cost:** experts take one more interaction.

## D9 — Existing themes instead of fixed palette

**Alternatives:** custom fixed palette or brand stylesheet; existing `QPalette`, theme manager, icon theme and platform metrics. **Chosen:** use current semantic roles. **Reason:** multiple built-in and user custom themes exist; fixed colors clash and fail contrast. **Cost:** exact rendering varies by theme; review each.

## D10 — Reuse provider browser

**Alternatives:** embedded new browser, dialog stack, existing Modrinth/CurseForge resource page. **Chosen:** reuse provider browser and carry selected resource/version to a single target step. **Reason:** existing search, filter, split detail and download conventions. **Cost:** context handoff needs wiring; do not promise batch/atomic support unless present.
