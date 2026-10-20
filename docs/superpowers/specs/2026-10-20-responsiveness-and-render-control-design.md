# Milestone 2B: Responsiveness and Render Control Design

**Date:** 2026-10-20
**Status:** Approved in conversation; awaiting written-spec review

## Purpose

Make the private TI-84 Plus CE application feel responsive and trustworthy on real hardware. Fast sequential keypresses must not disappear, ordinary expression editing must not repeatedly restart graph work, and every graph surface must say whether it is loading, current, only a preview, canceled, or out of date.

This milestone also adds per-equation visibility, one-button relation cycling, calculator-consistent inverse-trigonometry keys, and a private diagnostics page for physical timing and RAM measurements. It does not add the paper-style math editor, function catalog, numerical intercept tools, or a public/shareable build.

## User Context and Confirmed Hardware Findings

- Target: TI-84 Plus CE Python running arTIfiCE Shell 2.1.
- The user observed dropped fast sequences such as `X` followed immediately by `+`.
- The user observed visible delays during expression entry and when entering Graph or Split.
- The user could not distinguish a frozen program from an active graph calculation.
- The current private build reports approximately 117,000 bytes of free calculator RAM outside the program, but the linked program exposes only about 47,300 bytes of ordinary C heap after static storage.
- One uncompressed 320-by-240 8-bit screen requires 76,800 bytes. A third full-screen bitmap is therefore not safe in ordinary application memory.
- The UI remains private. No upload, publication, release package, push, merge, or shareable artifact is authorized.

## Scope

### Included

- Preserve overlapping and fast sequential keypresses without repeating held keys.
- Poll and handle input before expensive UI or graph work.
- Separate expression-pane redraws from graph invalidation.
- Show truthful three-stage render progress and per-equation progress.
- Allow safe render cancellation and explicit reload.
- Track graph freshness so stale geometry is never presented as current.
- Toggle committed relation visibility with `2nd` then `ENTER`.
- Persist visibility in saved project format version 3 while migrating versions 1 and 2.
- Cycle relation operators with the `MATH` key.
- Map the printed inverse-trig keys to `arcsin`, `arccos`, and `arctan`.
- Add a private, read-only diagnostics page for RAM and render timings.
- Preserve the approved view keys, menu behavior, editing controls, save/load safety, and `2nd` then `MODE` quit path.

### Deferred

- Paper-style superscripts, radicals, fraction boxes, absolute-value boxes, and variable-height expression rows
- The `2nd`+`0` function catalog
- Zeros, intercepts, intersections, trace, extrema, and numerical analysis
- Inequality shading and other new graph classes
- Degree mode; existing and new trigonometric functions remain radians
- A third bitmap, compressed graph cache, retained segment cache, or persistent layout cache
- Automatic saving, time-remaining estimates, and user-selectable render quality
- 3D rendering
- Public packaging or distribution

## Approved Controls

### Existing Controls Preserved

- `Y=` opens Expressions.
- `WINDOW` opens Compact Split.
- `ZOOM` toggles the existing 2D/3D mode.
- `TRACE` opens or closes the project menu.
- `GRAPH` opens Graph.
- `2nd`, then `MODE` requests quit.
- While editing, `DEL` removes one source character and `CLEAR` clears the selected expression.
- Outside editing, arrow keys select rows in Expressions/Split and pan in Graph as already approved.

### Equation Visibility

- While not editing, `2nd` then `ENTER` toggles the selected committed relation.
- A filled slot-colored marker means visible.
- A hollow or muted marker means hidden.
- Definitions and scalar results do not receive a graph-visibility marker and the toggle is a no-op for them.
- A hidden relation remains editable and evaluable by project logic; only the graph scheduler skips it.
- Hiding a relation never disables definitions that other expressions use.
- Clearing a slot resets that slot to visible so a later new relation is not unexpectedly hidden.

### Render Cancel and Reload

- While Graph or Split is actively loading and no expression editor is active, `DEL` cancels at the next safe row boundary.
- While editing, `DEL` always retains its character-deletion meaning and never cancels rendering.
- Canceling never removes or changes an expression, viewport, save, or dirty flag.
- Pressing the active view key reloads a canceled or out-of-date surface:
  - `GRAPH` reloads Graph.
  - `WINDOW` reloads Split.
- Re-pressing the current view key while its graph is already current is a no-op.
- Selecting Graph or Split from another page is an explicit load request.

### Relation Cycling

- Unmodified `MATH` inserts `=` when the caret is not adjacent to a complete relation token.
- When the caret is immediately before, inside, or immediately after one complete token, `MATH` replaces the whole token and cycles:

  `=` -> `<` -> `>` -> `<=` -> `>=` -> `=`

- The token immediately to the left of the caret wins; otherwise a token starting at the caret is used.
- The caret ends immediately after the replacement.
- Invalid clusters such as `==`, `<<`, and `<==` are not partially rewritten.
- If a one-byte token cannot grow to two bytes because the editor is full, nothing changes.
- `ALPHA`+`MATH` remains the letter `a`.
- The old invented `2nd` relation shortcuts are removed; relation entry uses the single `MATH` cycle.
- Until paper-style math is implemented, `<=` and `>=` remain visible as ASCII source.
- This milestone does not make inequalities drawable; the parser stores them and the equality renderer continues to skip them.

### Inverse Trigonometry

- `2nd`+`SIN` inserts `arcsin(`.
- `2nd`+`COS` inserts `arccos(`.
- `2nd`+`TAN` inserts `arctan(`.
- `arcsin` and `arccos` return a domain error outside `[-1,1]`.
- `arctan` accepts every finite input.
- Results use radians, consistent with the current `sin`, `cos`, and `tan` implementation.
- The current `abs`, `exp`, and `floor` second-key shortcuts are removed. Those names remain typeable through alpha input and will receive catalog entries in the next milestone.
- The later paper-style renderer will display these functions as `sin^-1`, `cos^-1`, and `tan^-1` without changing saved source.

## Input Responsiveness

### Confirmed Root Cause

The current scanner detects all rising edges in the keypad matrix but returns only the first one while marking every detected group as already handled. If `X` and `+` overlap in one sample, `X` wins by scan order and `+` is permanently lost. The current loop also performs a pending full redraw before its next input poll, creating an additional interval in which a complete press and release may be missed.

### Pending Edge Buffer

The CE input adapter owns two fixed eight-byte arrays:

- the previously sampled keypad groups, and
- pending rising-edge bits not yet delivered.

Each poll:

1. samples all keypad groups,
2. ORs every new rising edge into the pending array,
3. updates the previous sample,
4. selects one pending edge,
5. clears only that delivered edge, and
6. dispatches it through the existing editing/action rules.

`2nd` and `ALPHA` receive priority over ordinary keys captured in the same sample so their intended modifier applies. A held key contributes only its original rising edge, so it cannot auto-repeat accidentally. Distinct queued keys survive redraws and are handled one per main-loop iteration.

When a delivered key changes application context—view switch, menu, quit, mode switch, load, or new project—remaining pending ordinary edges are cleared. This prevents a simultaneous arrow or editing key from leaking into a new page or modal dialog.

The pending edge buffer costs eight additional bytes of static storage and performs no allocation.

The bitset preserves every distinct key edge seen by a scan, but hardware cannot reveal the original order when two ordinary keys first appear in the exact same sample. Those ties use a deterministic keypad order. It also cannot reconstruct a complete press-and-release that happens entirely between polls. If physical testing still exposes either problem after poll-first rendering, the input layer must be upgraded to an ordered interrupt-fed ring; that larger mechanism is not added speculatively.

### Poll-First Main Loop

The main loop polls and handles one input event before any redraw or render row. After handling input it performs, in order:

1. a bounded expression/header UI refresh if requested,
2. a full layout/grid refresh if requested,
3. at most one graph-render row when no higher-priority UI work remains.

No graph row, screen fill, or buffer swap occurs before the input poll. Modal dialogs continue to own input exclusively and never allow background panning or rendering.

## UI Redraw Versus Graph Invalidation

The main loop distinguishes at least two independent flags:

- **UI dirty:** selection, caret, draft text, status label, or another non-geometric visual changed.
- **Graph dirty:** committed graph geometry, visibility, viewport, graph rectangle, mode, or project changed.

### UI-Only Changes

These update the expression pane and/or header without canceling the active graph job:

- moving the selected expression,
- moving the editor caret,
- inserting or deleting draft source,
- displaying a draft parse/validation error while the prior committed graph remains active,
- updating render progress text,
- pressing a selection key at a boundary where nothing changes,
- re-pressing an already-current page key while the graph is current.

In Split, the expression rectangle is redrawn and copied to both GraphX frame roles without touching the graph rectangle. This keeps the visible pane and the hidden in-progress frame consistent across the next swap. Expressions view has no graph job and may redraw its full content area.

### Graph Changes

These cancel stale work, increment the render generation, and either start loading or mark the surface out of date according to the manual-cancel rule:

- successful expression commit,
- clearing a committed expression,
- toggling expression visibility,
- pan or zoom,
- Graph/Split layout switch,
- 2D/3D mode switch,
- new project,
- loaded project.

Opening a modal dialog pauses/cancels active graph work because the dialog overwrites display buffers. Closing the dialog restores the page and starts a fresh render unless the user has manually canceled that surface.

Draft typing never changes the committed graph generation. A failed draft commit continues displaying the draft error while the retained committed equation remains the render source, matching Milestone 2A behavior.

## Render State Machine

### Freshness States

Every graph surface exposes one truthful state:

- **Current:** refined geometry matches the current project, visibility, viewport, mode, and graph rectangle.
- **Loading:** a coarse or refined job for the current generation is active.
- **Preview only:** coarse geometry matches the current generation, but refinement is not complete.
- **Canceled:** no completed coarse geometry exists for the current generation.
- **Out of date:** visible geometry or grid does not represent the current generation and is not loading.
- **Unable to load:** bounded validation or render startup failed; the job is idle and may be retried explicitly.

The render controller carries a generation stamp. A job may draw or swap only when its captured generation still equals the current graph generation. This is defense against stale queued work; it is not saved in the project.

### Three Stages

The header shows the stage and actual drawable-equation count:

1. `1/3 Coarse 2/5`
2. `2/3 Release keys`
3. `3/3 Refining 2/5`

Only visible, drawable equalities count toward the numerator and denominator. Blank, hidden, invalid, undefined, cyclic, 3D, definition, scalar, and currently unsupported non-equality lines are excluded.

Stage 2 remains visible while a graph-navigation key is held. Without a held key it may last only one main-loop iteration before refinement starts. No fake percentage or time estimate is shown.

The header also shows `DEL: Cancel` while cancel is available.

### Cancellation Results

- Cancel during stage 1 stops before the next row and leaves the grid labeled `Canceled - not loaded`.
- Cancel after a completed coarse pass, including stages 2 or 3, preserves the matching coarse frame and labels it `Canceled - preview only`.
- A subsequent equation, visibility, or viewport change after manual cancellation clears mismatched curves and labels the surface `Out of date - press GRAPH` or `Out of date - press WINDOW`.
- Ordinary commits automatically start loading when no manual cancellation is active.
- After manual cancellation, automatic graph restarts remain suppressed for that surface until the user presses its active view key, selects that graph view from another page, starts a new project, or loads a saved project.
- Cancel is processed between row batches, never in the middle of an expression evaluation or segment callback.

### Buffer Strategy

GraphX's existing screen and draw buffer remain the only full frames.

- The static grid appears immediately.
- Coarse geometry is constructed in the hidden buffer and swapped only when its current-generation coarse pass completes.
- Fine geometry is reconstructed in the hidden buffer and swapped only when its current-generation refined pass completes.
- Header and Split expression-pane changes are mirrored into both frame roles so a later swap cannot restore stale UI text.
- No third bitmap, retained segment list, or compressed frame is allocated.

Graph and Split always render separately because their graph rectangles differ. A Graph image cannot safely seed Split: clipping and axis-label placement depend on the rectangle.

## Equation Visibility and Project Format

### Project State

`Project` gains a 16-bit visibility mask, one bit per expression slot.

- Initialization sets all 16 bits visible.
- Toggling affects only a committed relation slot.
- Project evaluation and dependency resolution ignore visibility.
- The graph scheduler requires both mathematical drawability and a visible bit.
- Slot colors remain based on original slot index, so hiding slot 1 does not recolor slots 2 and 3.
- Clearing a slot resets its bit to visible.
- Replacing or loading a project transfers the mask transactionally with the rest of the owner.

The public project boundary provides visibility queries/toggling; the renderer and UI do not reach into line storage.

### Saved Format Version 3

Version 3 appends the 16-bit visibility mask to the existing version 2 metadata. The maximum codec and atomic-store work buffer grow by two bytes.

- Version 1 loads with the default viewport and all expressions visible.
- Version 2 preserves its saved viewport and loads all expressions visible.
- Version 3 preserves viewport and visibility.
- Existing CRC, bounds, duplicate-definition, allocation-failure, and transactional replacement rules remain unchanged.
- Corrupt or unsupported input cannot partially update the active project.
- The selected invalid draft, if any, continues to round-trip as source text.

The decoder must explicitly recognize both versions 2 and 3 as carrying viewport fields; it must not key viewport decoding only to the newest version constant.

Visibility changes mark the project dirty because they are saved project behavior. Viewport movement retains its existing rule: it does not independently trigger an unsaved-data prompt, but the current viewport is written during an explicit save.

## Private Diagnostics

The project menu gains a read-only `Diagnostics` item in private builds. It displays:

- free ordinary RAM at the time the page opens,
- last completed coarse time,
- last completed refinement time,
- longest observed row-batch time,
- render restart count,
- user cancel count,
- last completed drawable-equation count,
- current freshness state.

Timing uses the CE toolchain's monotonic `clock()` ticks and never updates the display while a row is being timed. Counters saturate rather than wrap into misleading small values. Diagnostics are fixed-size, allocate nothing, are not persisted, and do not change project dirty state.

Host fakes supply deterministic clock and memory values. Before any public release, diagnostics will either be removed or deliberately hidden behind a development setting; this milestone creates no public build.

## Error Handling and Safety

- Input capture and render state use fixed-size storage only.
- Failed insertion, relation growth at capacity, or unsupported key chords leave the editor unchanged.
- Invalid inverse-trig domains return the existing domain error, not a non-finite result.
- A render startup error transitions to `Unable to load` and stops; it does not spin.
- A stale generation can never swap its frame onscreen.
- Cancellation and diagnostics never mutate project data.
- Visibility codec failures preserve the current project and prior save through the existing atomic-store protocol.
- Menu navigation remains modal and cannot pan or zoom the hidden graph.
- The existing quit confirmation and save/discard/cancel behavior remain authoritative.

## Testing Strategy

All behavior changes follow strict red-green test-driven development.

### Input Tests

- Overlapping `X` and `+` produce `x+` across two polls instead of losing `+`.
- A held key inserts once; release/repress inserts once more.
- Simultaneous modifier and ordinary key applies `2nd`/`ALPHA` first.
- View/menu/context actions clear unsafe queued edges.
- Top view keys, menu toggle, graph navigation, edit/delete/clear, and quit regressions remain green.
- MATH inserts, cycles, wraps, handles caret positions, rejects invalid clusters, and respects capacity.
- `arcsin`, `arccos`, and `arctan` key mappings insert exactly once.

### Math Tests

- Parser/name round-trips for all three arc names.
- `arcsin(0)`, `arcsin(1)`, `arccos(1)`, `arccos(0)`, and representative `arctan` values.
- Domain errors outside `[-1,1]` and non-finite propagation.
- Definitions, dependencies, coordinate evaluation, and saved-source round-trips using arc functions.

### Visibility and Codec Tests

- Visibility defaults, toggles, no-op targets, clear reset, slot colors, and hidden scheduler gaps.
- Visibility bits on definitions are ignored and do not alter dependent evaluation.
- Invalid drafts preserve the committed line's visibility and graph behavior.
- Version 1 and 2 migrate all-visible; version 3 round-trips arbitrary masks.
- Truncation, bad CRC, bad version, allocation failure, and atomic recovery remain transactional.
- Codec maximum-size fixtures include the two version 3 bytes.

### Render and UI Tests

- Input is polled before UI redraw and before every row batch.
- Split selection, caret, and draft changes do not restart the graph job.
- Commits, clear, visibility, viewport, layout, mode, new, and load invalidate exactly once.
- No-op actions do not restart.
- Stage labels, equation counts, cancel hint, and final label removal appear on the correct visible frame.
- Cancel during coarse yields not-loaded; cancel during fine preserves only a labeled coarse preview.
- Manual cancel suppresses automatic restarts until an explicit reload action.
- Generation mismatch prevents stale swaps.
- Error startup terminates in `Unable to load`.
- Diagnostics use deterministic fake time/RAM and never mutate the project.

### Whole-Program Verification

- Run the complete host suite from the existing 55,032-check baseline.
- Perform a clean CE build of every source file.
- Inspect generated map/static sizes for the input buffer, visibility mask, render controller, diagnostics, total BSS, and remaining heap.
- Accept only the two already disclosed CE toolchain GNU-stack metadata warnings.
- Request independent review after each implementation task and a final whole-branch review.

## Physical Calculator Checklist

- Type repeated fast sequences, including `X`, `+`, digits, parentheses, and modifiers, in Expressions and Split.
- Confirm held keys do not repeat unexpectedly.
- Confirm Split typing and caret movement do not restart or erase graph progress.
- Record static-frame, coarse, and refinement times for 0, 1, 5, and 16 equations.
- Confirm `1/3`, `2/3`, and `3/3` states are honest and the final state says current.
- Cancel during coarse and fine, verify the correct not-loaded/preview result, then reload with `GRAPH` or `WINDOW`.
- Change an equation, visibility, pan, and zoom after cancel; confirm stale curves are cleared and the surface stays out of date until explicit reload.
- Toggle equations 1, 2, and 3 independently and verify remaining slot colors do not shift.
- Save/load version 3 and load existing version 1/version 2 projects.
- Test `MATH` cycling and all three arc-function keys.
- Read diagnostics after representative line, vertical line, circle, ellipse, rotated ellipse, and `y=1/x` renders.
- Record free RAM and recovery after repeated edit, toggle, cancel, reload, clear, save, and load cycles.
- Verify menu navigation, quit, Save/Discard/Cancel, invalid drafts, and memory-error recovery remain responsive.

## Success Criteria

- Fast overlapping keys captured in one scan are delivered instead of discarded.
- No graph work occurs before the main-loop input poll.
- Editing an uncommitted draft does not restart committed graph rendering.
- The user can always distinguish loading, preview, canceled, out-of-date, error, and current states.
- Cancel is safe, bounded to row boundaries, and followed by an explicit reload path.
- Hidden equations stay hidden across saves without changing definition semantics or slot colors.
- Old projects load safely with every equation visible.
- Physical inverse-trig keys insert and evaluate the approved `arc` names.
- The implementation adds no dynamic render allocation or third bitmap.
- Automated verification, clean CE build, map inspection, independent review, and the physical checklist all pass before Milestone 2B is called complete.
- No shareable or public release is created without the user's later explicit approval.
