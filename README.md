# DGCALC — private calculator test build

DGCALC is an original offline graphing-calculator project for the TI-84 Plus CE Python running arTIfiCE Shell 2.1. This is the Milestone 2B private hardware test build, not a release. Do not upload, publish, push, merge, distribute, or create a public/shareable package. Keep the local `.8xp` inside this worktree for the user's private calculator test.

The general expression engine accepts relations in either orientation, including `25=x^2+y^2` and `x^2+y^2+z^2=16`. Explicit, vertical, reversed, circle, ellipse, rotated ellipse, and general implicit 2D equalities share the renderer. 3D remains a labeled placeholder; stored inequalities are not drawn.

All 16 expression positions remain available, with 128 AST nodes allowed per expression. Blank slots own no expression payload; populated slots retain committed source and used nodes. One shared editor and one row-based render job serve every slot. The two existing GraphX frames are the only full-screen buffers: there is no third bitmap or per-expression render cache. Physical speed, RAM safety, and appearance are awaiting user verification.

## Build and host checks

The calculator build is pinned to CEdev v15.0. Its default TI-file comment contains a timestamp; use this fixed private comment for reproducible clean builds:

```powershell
$env:CEDEV_HOME = 'C:\CEdev'
$env:COMMENT = 'DGCALC private hardware test - CEdev v15.0'
powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1
```

The private output is `bin/DGCALC.8xp`, ignored by Git. Transfer it with TI Connect CE for the private hardware test, then launch DGCALC through arTIfiCE Shell 2.1.

Host tests use the workspace Zig compiler as a self-contained C11 driver:

```powershell
$env:HOST_CC = 'C:\Users\Adrian\Documents\CHAT GPT PROJECTS\.toolchains\zig-x86_64-windows-0.17.0\zig.exe'
powershell -ExecutionPolicy Bypass -File tools/test-host.ps1
```

## Calculator controls

- `Y=` opens Expressions, `WINDOW` opens compact Split, `ZOOM` toggles 2D/3D, `TRACE` opens/closes the project menu, and `GRAPH` opens Graph.
- Outside editing, up/down selects a row in Expressions/Split. `ENTER` starts editing the selected line and commits it when pressed again. In the editor, left/right moves the caret, `DEL` removes one source character, and `CLEAR` erases the entire selected expression. A failed commit retains the draft error and prior committed graph.
- In Graph, arrows pan, `+` zooms in, `-` zooms out, and `CLEAR` is ignored. Viewport movement alone does not trigger an unsaved-data prompt; explicit saves still store the viewport.
- Outside editing, `2nd`, then `ENTER` toggles the selected committed 2D/3D relation. A filled slot-colored marker means visible; a hollow marker means hidden. Blank lines, definitions, and scalar results have no visibility toggle. Hidden relations remain editable/evaluable; definitions still feed dependents. Slot colors do not shift. Clearing a relation restores its slot to visible.
- `2nd`, then `MODE` (`QUIT`) requests exit from every app screen, including menus and Diagnostics. Unsaved New, Load, and Quit operations require Save, Discard, or Cancel.
- Number keys, decimal point, arithmetic keys, parentheses, comma, `x²`, `^`, `sin`, `cos`, `tan`, `ln`, and `log` insert source text. `X,T,θ,n` inserts `x`. `ALPHA` plus the printed letter keys inserts `a` through `z`, including `ALPHA`+`1` for `y` and `ALPHA`+`2` for `z`.
- Unmodified `MATH` inserts `=` away from a relation; immediately before, inside, or after a complete token it cycles `=` → `<` → `>` → `<=` → `>=` → `=`. The left-adjacent token wins and the caret ends after the replacement. Invalid clusters (`==`, `<<`, `<==`) and growth beyond editor capacity leave source/caret unchanged. `ALPHA`+`MATH` remains `a`. The old `2nd`+MATH/APPS/PRGM/VARS relation shortcuts are removed. Inequalities remain ASCII source and are not plotted.
- `2nd`+`x²` inserts `sqrt(`. `2nd`+`SIN`, `COS`, and `TAN` insert `arcsin(`, `arccos(`, and `arctan(`. All trigonometry uses radians. Arcsine/arccosine reject values outside `[-1,1]`; arctangent accepts finite inputs. The former `abs(`, `exp(`, and `floor(` second-key shortcuts are removed; those names remain available through alpha entry.
- The `TRACE` menu contains New project, Save project, Load project, Toggle 2D / 3D, Quit, and Diagnostics. Up/down selects and `ENTER` chooses; `TRACE` or `CLEAR` closes it. Modal navigation cannot pan or zoom the background graph.

Captured overlapping key edges are delivered once across successive polls; held keys do not repeat. Modifiers have priority within a scan. Simultaneous ordinary keys use deterministic keypad order; a complete press/release between polls cannot be reconstructed. Real typing speed still needs physical testing.

## Loading, cancel, and freshness

Graph and Split draw a grid immediately, build the 8-pixel coarse pass in the hidden frame, then rebuild the 4-pixel fine pass there. Only completed passes swap into view. Each loop polls input before UI or graph work and processes at most one graph row. Split typing, caret movement, row selection, failed commits, and clearing an uncommitted draft refresh the UI without restarting committed graph work.

The three stages are `1/3 Coarse n/total`, `2/3 Release keys`, and `3/3 Refining n/total`. Counts include only visible drawable 2D equalities, excluding hidden, blank, invalid, undefined, cyclic, definition, scalar, 3D, and inequality lines. Without held navigation keys, stage 2 appears as `2/3 Preview` and may last one loop iteration. `Preview` also labels the coarse frame during refinement. No estimated percentage or time remaining is shown.

While loading and outside editing, `DEL` cancels between safe row batches; `DEL: Cancel` is shown while available. Canceling coarse work leaves the grid without a completed graph. Canceling after coarse completion preserves the matching coarse preview. Both show `Canceled - GRAPH/WINDOW to load`; the retained coarse frame additionally says `Preview`. Cancel does not change expressions, viewport, saves, or project dirty state. In the expression editor, `DEL` keeps its deletion meaning.

After manual cancellation, commits, visibility changes, and viewport changes clear mismatched curves and show `Out of date - GRAPH/WINDOW to load`. Automatic reload remains suppressed until an explicit request: press `GRAPH` for Graph or `WINDOW` for Split, select that graph view from another page, start a new project, or successfully load a project. Visiting Expressions alone does not reload. Re-pressing an already-current view key is a no-op. Without manual cancellation, successful graph changes load automatically.

Only a completed fine pass says `Current`; failed startup/render says `Unable to load` and stops until retried. Modal dialogs overwrite unfinished buffers, so returning restores the page and reloads unless manual cancellation is latched. Graph and Split render separately because their rectangles differ. Generation checks block obsolete work from drawing or swapping.

## Project saves

Projects use the archived AppVar `DGCPROJ`. Saves use `DGCTMP` and `DGCBAK` so failed writes, validation, rename, or archive operations can restore the previous project. Corrupt/unsupported saves and allocation failures leave the active project intact.

Version 1 loads with the default viewport and all slots visible. Version 2 retains its saved viewport and defaults all slots visible. Version 3 saves retain viewport and a 16-bit visibility mask; two mask bytes follow version 2 metadata. A selected invalid draft continues to round-trip as source. Visibility changes mark the project dirty; viewport movement alone does not.

## Private Diagnostics and verification record

Open `TRACE` → Diagnostics. It reports Free RAM (bytes) when opened, last completed Coarse (ticks), last completed Fine (ticks), Longest row (ticks), Render restarts, User cancels, last completed Drawable equations count, and Freshness. The CE clock runs at **32,768 ticks/second**. Stage times include intervening input/UI time; fine timing excludes the coarse-preview release wait. Longest row times one scheduler batch. Interrupted stages retain prior completed samples. Restart/cancel counters saturate at 65,535 and reset on app startup. The page allocates no history and does not change the project or save. `ENTER`, `CLEAR`, or `TRACE` closes it; the quit chord remains available.

- CEdev v15.0, eZ80 Clang 19.1.0; target arTIfiCE Shell 2.1. Calculator OS version is pending.
- Final clean host suite: **65,890 checks, zero failures, zero compiler warnings**.
- Two independent clean CE builds with fixed comment: **52,044 bytes each**, identical SHA-256 `B636D2E289079569AE38D96B597D2A5A0EFA7D71C5F65C0A4B795FA74E2E7712`.
- CE compilation has zero C warnings; linking emits the two known missing `.note.GNU-stack` metadata warnings for `atomic_load_increasing_32.o` and `obj/crt0.o`.
- Map: pending keys 8 bytes; visibility mask 2 bytes inside the 898-byte Project; render controller 866 bytes including the 848-byte scheduler; diagnostics 22 bytes; total BSS **13,442 bytes**; linked ordinary C heap region **47,248 bytes** before runtime allocations. GraphX uses its existing 153,600-byte LCD VRAM region for two 76,800-byte frames outside application BSS/heap.
- The user's approximately 117,000 bytes of free calculator RAM is external context, not a measurement of this build's available C heap. Static bounds and host tests do not establish physical speed, RAM safety, or final approval.

## Pending private hardware checklist

All items remain **awaiting user verification** on the TI-84 Plus CE Python; record calculator OS, shell version, free RAM, and results.

- [ ] Type fast `X` then `+`, digits, parentheses, and same-scan modifier chords in Expressions and Split. Verify held keys do not repeat and typing/caret movement during loading does not erase/restart progress.
- [ ] Plot `y=x`, `x=2`, `x^2+y^2=25`, `25=x^2+y^2`, `9x^2+4y^2=36`, rotated ellipse `5x^2+6x*y+5y^2=16`, and `y=1/x` in Graph/Split. Check clipping, grid/axes/labels, slot colors, and no false asymptote bridge. Read Diagnostics after each representative shape.
- [ ] Record static-frame, coarse, and fine times with 0, 1, 5, and 16 equations. Observe all three stages, actual counts, Preview, and final Current; hold/release navigation during stage 2.
- [ ] Cancel during coarse, release-wait, and fine; confirm not-loaded versus retained-preview results. Verify editor DEL deletes text. Commit/edit, toggle, pan, and zoom after cancel; confirm stale curves clear and explicit GRAPH/WINDOW reload is required. Return from Expressions; test repeated current-page keys.
- [ ] Toggle relations 1/2/3; confirm markers, unchanged remaining colors, saved hidden state, unaffected definitions, and visible defaults after clear.
- [ ] Test MATH insertion/cycling/wrap, caret positions, invalid clusters, and full-buffer growth. Test all three inverse-trig keys, radians, and domain errors.
- [ ] Open/close menus and Diagnostics during loading; confirm navigation isolation, restoration, manual-cancel preservation, and quit via `2nd` then `MODE`. Verify Save/Discard/Cancel, invalid drafts, and memory-error recovery.
- [ ] Reset/reopen; load version 1 and existing version 2 saves (including viewport migration); save/reload version 3 viewport, visibility, and selected invalid draft.
- [ ] Record free RAM with 0, 1, 5, and 16 equations and recovery after repeated edit, toggle, cancel, reload, clear, save, and load cycles, including clearing all lines. Do not infer runtime safety from linked heap size alone.

## Deferred

Paper-style math (superscripts, radicals, fractions, absolute-value boxes, variable-height rows), the `2nd`+`0` function catalog, intercept/zero/intersection/trace/extrema tools, inequality shading, polar/parametric and other graph classes, degree mode, 3D rendering, automatic saves, time-remaining estimates, selectable render quality, extra bitmap/segment/compressed/layout caches, and public packaging/distribution remain deferred. Diagnostics needs a deliberate remove/hide decision before any public release.
