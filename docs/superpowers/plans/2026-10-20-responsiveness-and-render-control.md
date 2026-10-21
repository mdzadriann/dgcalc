# Milestone 2B: Responsiveness and Render Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (the user's chosen method) to implement this plan task-by-task. Give each task to a fresh implementer, run a specification review and a code-quality review before moving on, and use `superpowers:verification-before-completion` before any completion claim.

**Goal:** Make the private TI-84 Plus CE graphing app respond reliably to fast input, keep completed graph pixels across ordinary UI changes, expose honest and cancelable render progress, and add visibility toggles, relation cycling, inverse trigonometry, save migration, and private diagnostics.

**Architecture:** Preserve the existing fixed-memory parser, two GraphX draw buffers, row-batched plotter, and project model. Fix key loss once in the CE input adapter with a pending-edge bitset; distinguish UI refreshes from graph reloads in the existing main/UI path; extend the existing project scheduler and codec rather than adding caches or frameworks. Keep the render state machine inside `ce_ui.c`, where the current progressive renderer already lives.

**Tech Stack:** C11; CEdev v15.0; CE libraries `graphx`, `keypadc`, and `fileioc`; existing PowerShell host-test and calculator-build scripts; no new dependency or assembly.

**Spec:** `docs/superpowers/specs/2026-10-20-responsiveness-and-render-control-design.md`

## Global Constraints

- Target the user's TI-84 Plus CE Python running arTIfiCE Shell 2.1 with approximately 117,000 bytes of free calculator RAM.
- Reuse the existing two GraphX buffers. Do not add a third bitmap, retained segment cache, compressed graph cache, or per-slot graph image.
- Keep all 16 expression positions but allocate no expression payload for blank positions.
- Poll and handle input before full-page drawing or a graph row.
- Editing, cursor movement, row selection, visibility marker refresh, and no-op actions must not restart graph work.
- A mathematical or viewport change makes the graph out of date. After manual cancellation it remains unloaded until the user explicitly presses the active `GRAPH` or `WINDOW` key, returns to that graph view from another page, starts a new project, or loads a saved project.
- A coarse frame is labeled as a preview; only a completed fine pass is current.
- A stale render generation must never swap its pixels onto the visible screen.
- `DEL` deletes characters while editing and cancels active rendering only while not editing. `CLEAR` keeps its existing whole-expression behavior.
- `2nd`, then `ENTER` toggles only committed 2D/3D relations. Clearing a line restores that slot's default visible state.
- Trigonometry and inverse trigonometry remain radians.
- Versions 1 and 2 load transactionally with all committed relations visible; version 3 writes the visibility mask without losing the version 2 viewport.
- Preserve menu-modal input isolation and the existing `2nd`, then `MODE` quit path.
- Use ordinary C. Add no dependency, assembly, paper-math editor, catalog, numerical intercept tool, inequality shading, or 3D renderer in this milestone.
- Create private test `.8xp` files only. Do not upload, publish, push, merge, release, or create a public/shareable package.

## Review Focus

- A same-scan modifier plus key, or `X` plus `+`, must be delivered exactly once without leaking an ordinary key after a view/menu action; pinned in Task 1.
- Cancel, edit, commit, pan, menu return, and active-view reload must preserve the manual-reload rule; pinned in Tasks 6 and 7.
- Version 1/2 decoding must preserve old content and version 2 viewports while defaulting visibility to all-on; CRC, truncation, unknown versions, and allocation failure remain transactional; pinned in Task 5.
- Hidden relations remain editable and evaluable, definitions still feed dependents, and only drawing is skipped; pinned in Task 4.
- Coarse/fine swaps, partial Split refreshes, and render generations must not expose stale geometry, stale status text, or a mismatched expression pane; pinned in Tasks 6 and 7.
- Diagnostics must measure rather than estimate free RAM and render timings, and must stay private; pinned in Task 8.

---

## Planned File Structure

```text
src/math/ast.h, ast.c                      arcsin/arccos/arctan names
src/math/eval.c                            inverse-trig evaluation in radians
src/project/project.h, project.c           visibility mask and guarded toggle
src/graph/project_plot2d.h/.c              hidden-line skipping and equation progress
src/storage/project_codec.h/.c             version 3 visibility persistence and migration
src/app/app.h, app.c                       visibility/cancel actions and change classification
src/platform/ce/ce_input.h/.c              queued rising edges, relation cycling, key mappings
src/platform/ce/ce_ui.h/.c                 render state, progress, cancellation, refresh, diagnostics
src/main.c                                  input-first loop and UI-vs-graph invalidation routing
tests/fake_ce.h, fake_ce.c                  deterministic keypad, clock, RAM, and GraphX fakes
tests/*_test.c                              focused regression and migration coverage
README.md                                   private controls, states, and hardware checklist
```

## Task 1: Preserve Every Captured Key Edge

**Files:**

- Modify: `src/platform/ce/ce_input.h`, `src/platform/ce/ce_input.c` (`previous_keys`, `first_new_key`, `ce_input_begin`, `ce_input_poll`)
- Modify: `tests/ce_input_test.c` (key-edge regressions)
- Modify if needed: `tests/fake_ce.h`, `tests/fake_ce.c` (multi-group keypad samples)

**Interfaces:**

- Preserve: `void ce_input_begin(void)`
- Preserve: `AppAction ce_input_poll(EditorBuffer *active_editor, bool editing, AppView view)`
- Add: `void ce_input_discard_pending(void)` for modal/new/load context boundaries.
- Add only internal storage: `static uint8_t pending_keys[8]`
- Replace the lossy “scan and return first” helper with one capture pass and one deterministic dequeue pass.

- [ ] **Step 1: Write the overlapping-edge tests**

  Add tests that press `X` and `+` in different keypad groups during one scan. The first poll inserts `x`, the next poll inserts `+` even though both physical keys remain held, and a third poll produces nothing. Add same-scan `2nd`+key and `ALPHA`+key cases, a release/repress case, and a view/menu action case proving queued ordinary keys are cleared rather than leaking into the next page.

- [ ] **Step 2: Run the host suite and confirm RED**

  Run:

  ```powershell
  $env:HOST_CC='C:\Users\Adrian\Documents\CHAT GPT PROJECTS\.toolchains\zig-x86_64-windows-0.17.0\zig.exe'
  powershell -ExecutionPolicy Bypass -File tools/test-host.ps1
  ```

  Expected: the new overlapping-edge assertion fails because `first_new_key()` currently marks every group handled while returning only one key.

- [ ] **Step 3: Implement the pending-edge bitset**

  On each poll, OR newly rising bits into `pending_keys`, update `previous_keys`, then dequeue exactly one bit using the existing deterministic group/key priority. Treat modifier edges first. Never requeue held keys. Clear pending ordinary edges after a context-changing page/menu action so input captured for one context cannot appear in another. Expose the same clear operation for main to call after modal, new-project, and load-project paths in Task 7.

  Add this required limit comment beside the bitset:

  ```c
  // shortcut: simultaneous ordinary keys use deterministic keypad order, upgrade to an interrupt ring only if physical tests require chronological ordering
  ```

- [ ] **Step 4: Run all host tests**

  Expected: all prior 55,032 checks and the new edge cases pass with zero failures.

- [ ] **Step 5: Build the private calculator program**

  Set `CEDEV_HOME` to the installed `C:\CEdev`, run `powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1`, and confirm `bin/DGCALC.8xp` is produced without new linker warnings. Do not distribute it.

- [ ] **Step 6: Commit**

  ```powershell
  git add src/platform/ce/ce_input.* tests/ce_input_test.c tests/fake_ce.h tests/fake_ce.c
  git commit -m "fix: preserve overlapping calculator keypresses"
  ```

## Task 2: Cycle Relation Operators In Place

**Files:**

- Modify: `src/platform/ce/ce_input.c` (`MATH` handling and removal of invented relation shortcuts)
- Modify: `tests/ce_input_test.c`

**Interfaces:**

- Add no public editor API; the one caller uses a small static transactional replacement helper in `ce_input.c`.
- Preserve the existing editor APIs and fixed `EDITOR_CAPACITY`.
- Normal `MATH` inserts `=` when the cursor is not on a relation operator; when it is on or immediately after one, it cycles `=` -> `<` -> `>` -> `<=` -> `>=` -> `=`.

- [ ] **Step 1: Write failing relation-cycle input tests**

  Cover all five transitions; caret immediately before, inside, and after a complete token; insertion of a new `=`; unrelated operators elsewhere; rejection of invalid clusters such as `==`, `<<`, and `<==`; and a full-buffer growth failure that leaves source and caret unchanged. Assert that former `2nd` relation shortcuts no longer insert `<`, `>`, `<=`, or `>=`.

- [ ] **Step 2: Run the host suite and confirm RED**

  Expected: relation-cycle assertions fail because unmodified `MATH` currently only inserts `=`.

- [ ] **Step 3: Implement the smallest transactional replacement and cycle**

  In `ce_input.c`, inspect at most the two characters adjacent to the caret, recognize one maximal valid relation token, validate the final length before mutation, and use `memmove` once to replace it. Leave the caret immediately after the replacement. If there is no adjacent complete token, insert `=` through the existing editor path.

- [ ] **Step 4: Run all host tests and commit**

  ```powershell
  git add src/platform/ce/ce_input.c tests/ce_input_test.c
  git commit -m "feat: cycle relation operators with math key"
  ```

## Task 3: Add Correct Inverse-Trigonometry Names and Keys

**Files:**

- Modify: `src/math/ast.h`, `src/math/ast.c` (three built-in names)
- Modify: `src/math/eval.c` (radian evaluation)
- Modify if required by the existing name range: `src/math/validate.c`, `src/math/deps.c`
- Modify: `src/platform/ce/ce_input.c` (`2nd` trig mappings; remove temporary invented mappings)
- Modify: `tests/parser_test.c`, `tests/eval_test.c`, `tests/project_test.c`, `tests/project_codec_test.c`, `tests/ce_input_test.c`

**Interfaces:**

- Add AST names immediately before `AST_NAME_COUNT`: `AST_NAME_ARCSIN`, `AST_NAME_ARCCOS`, `AST_NAME_ARCTAN`.
- Parse source spellings `arcsin(`, `arccos(`, and `arctan(`.
- Map `2nd`+`SIN`, `2nd`+`COS`, and `2nd`+`TAN` to those spellings.
- Do not introduce aliases or paper-style presentation in this task.

- [ ] **Step 1: Write failing parser/evaluator tests**

  Parse/name-round-trip all three names; evaluate `arcsin(0)`, `arcsin(1)`, `arccos(1)`, `arccos(0)`, and representative `arctan` inputs with existing floating-point tolerances; assert out-of-range arcsine/arccosine and non-finite inputs use the existing error paths.

- [ ] **Step 2: Write failing physical-key mapping tests**

  Assert the three printed inverse-trig sequences insert the approved names. Assert the removed temporary mappings no longer insert `abs(`, `exp(`, or `floor(`; those functions wait for the catalog milestone.

  Add project tests proving the arc functions work in definitions, dependencies, and coordinate evaluation, plus a codec round trip proving their source spellings are preserved without a format-specific special case.

- [ ] **Step 3: Run the host suite and confirm RED**

  Expected: the parser rejects the new names and the old `2nd` mappings fail the new expectations.

- [ ] **Step 4: Extend the existing built-in table and evaluator**

  Append the names without renumbering existing entries. Evaluate with `asinf`, `acosf`, and `atanf`; use the evaluator's existing non-finite/domain-error path.

- [ ] **Step 5: Run host tests, build CE, and commit**

  ```powershell
  git add src/math src/platform/ce/ce_input.c tests/parser_test.c tests/eval_test.c tests/project_test.c tests/project_codec_test.c tests/ce_input_test.c
  git commit -m "feat: add inverse trigonometry input"
  ```

## Task 4: Model Visibility and Skip Hidden Relations

**Files:**

- Modify: `src/project/project.h`, `src/project/project.c`
- Modify: `src/graph/project_plot2d.h`, `src/graph/project_plot2d.c`
- Modify: `tests/project_test.c`, `tests/project_plot2d_test.c`
- Modify callers of `project_plot2d_begin` in `src/platform/ce/ce_ui.c` and tests so the tree compiles; rendering behavior changes in Task 6.

**Interfaces:**

- Add `uint16_t visible_mask` to `Project`.
- Add:

  ```c
  bool project_line_is_visible(const Project *project, uint8_t index);
  bool project_toggle_line_visibility(Project *project, uint8_t index);
  uint16_t project_visibility_mask(const Project *project);
  void project_restore_visibility_mask(Project *project, uint16_t mask);
  ```

- Change scheduler start to `void project_plot2d_begin(ProjectPlot2D *plot, const Project *project, Viewport2D viewport, GraphRect rect, uint8_t stride)` so it can count visible drawable relations once.
- Add read-only scheduler queries for completed and total drawable equations.

- [ ] **Step 1: Write failing project visibility tests**

  Verify initialization is all-visible, toggling a committed 2D or 3D relation flips one bit and marks the project dirty, definitions/scalars/blanks are no-ops, clearing restores visibility, and a hidden relation remains editable/evaluable. Verify hiding a definition is impossible and its dependent relation still evaluates. Put an invalid draft over a hidden committed relation and prove the retained committed graph remains hidden.

- [ ] **Step 2: Write failing scheduler tests**

  Build visible and hidden relations together. Assert only visible drawable relations run, colors keep their original slot indices, completed/total progress excludes hidden/non-drawable lines, and zero visible relations finishes cleanly.

- [ ] **Step 3: Run the host suite and confirm RED**

  Expected: compilation fails for the new visibility/scheduler API.

- [ ] **Step 4: Add the mask at the project boundary**

  Initialize all 16 bits to visible, guard every public access by index, restrict toggling by committed expression role, and reset the cleared slot's bit. `project_restore_visibility_mask` is for validated codec input and must not mark a freshly loaded project dirty.

- [ ] **Step 5: Make the existing scheduler skip hidden lines**

  Scan with `project_line_is_visible()` before starting an expression. Count drawable visible relations at begin and increment completed only after an equation finishes. Do not allocate a second list or copy AST/source data.

- [ ] **Step 6: Run all host tests and commit**

  ```powershell
  git add src/project src/graph/project_plot2d.* src/platform/ce/ce_ui.c tests/project_test.c tests/project_plot2d_test.c
  git commit -m "feat: toggle equation visibility"
  ```

## Task 5: Persist Visibility in Project Format Version 3

**Files:**

- Modify: `src/storage/project_codec.h`, `src/storage/project_codec.c`
- Modify if byte limits are duplicated: `src/storage/atomic_store.c`
- Modify: `tests/project_codec_test.c`, `tests/atomic_store_test.c`

**Interfaces and format:**

- Change `PROJECT_FORMAT_VERSION` from `2` to `3`.
- Keep bytes `0..28` identical to version 2.
- Store little-endian `visible_mask` at bytes `29..30`; version 3 expressions begin at byte `31`.
- Increase the maximum encoded size by exactly two bytes.
- Decoder accepts versions 1, 2, and 3. Versions 1/2 restore all visibility bits; version 2/3 preserve viewport fields.

- [ ] **Step 1: Add exact-byte version 3 tests**

  Encode a project with alternating visibility, assert the version byte, mask offsets, expression start, length, and CRC round trip. Decode a hand-written version 3 fixture and assert content, viewport, selection, and visibility.

- [ ] **Step 2: Add migration and transactional failure tests**

  Decode fixed version 1 and version 2 fixtures and assert all slots default visible; specifically assert the version 2 viewport survives. Re-run truncated payload, invalid viewport, unknown version, CRC damage, allocator-failure, and atomic recovery/resave cases across versions 1/2/3 and assert the destination project and prior save remain unchanged on failure.

- [ ] **Step 3: Run the host suite and confirm RED**

  Expected: version remains 2 and mask assertions fail.

- [ ] **Step 4: Implement the two-byte extension**

  Preserve the existing header and version-specific metadata branches. Validate the complete payload and construct the replacement project before swapping it into the caller. Mask unused high bits if `PROJECT_MAX_EXPRESSIONS` ever becomes smaller than 16; do not otherwise reinterpret saved bits.

- [ ] **Step 5: Run all host tests and commit**

  ```powershell
  git add src/storage tests/project_codec_test.c tests/atomic_store_test.c
  git commit -m "feat: save equation visibility in format v3"
  ```

## Task 6: Make Rendering Truthful, Cancelable, and Generation-Safe

**Files:**

- Modify: `src/platform/ce/ce_ui.h`, `src/platform/ce/ce_ui.c`
- Modify: `tests/ce_ui_test.c`
- Modify if needed: `tests/fake_ce.h`, `tests/fake_ce.c`
- Modify only enough to compile new signatures: `src/main.c`

**Interfaces:**

- Add:

  ```c
  typedef enum {
      CE_RENDER_CURRENT,
      CE_RENDER_COARSE,
      CE_RENDER_RELEASE_KEYS,
      CE_RENDER_FINE,
      CE_RENDER_PREVIEW,
      CE_RENDER_CANCELED,
      CE_RENDER_OUT_OF_DATE,
      CE_RENDER_ERROR
  } CeRenderState;

  typedef struct {
      CeRenderState state;
      uint8_t completed;
      uint8_t total;
      bool manual_reload_required;
  } CeRenderStatus;

  void ce_ui_draw(const AppState *state, const Project *project,
                  bool editing, bool load_graph);
  void ce_ui_refresh(const AppState *state, const Project *project,
                     bool editing);
  bool ce_ui_cancel_render(void);
  CeRenderStatus ce_ui_render_status(void);
  ```

- Preserve: `bool ce_ui_render_step(const AppState *, const Project *, bool navigation_active)`.
- `ce_ui_draw(..., true)` begins a new generation; `ce_ui_draw(..., false)` draws an explicitly stale/canceled graph surface without starting work. `ce_ui_refresh` updates only header/expression/status regions in both buffers.

- [ ] **Step 1: Write failing state-transition tests**

  Cover `1/3 Coarse`, `2/3 Release keys`, `3/3 Refining`, preview after coarse completion, current only after fine completion, per-equation counts, error, cancel during coarse, cancel during fine, and zero drawable equations. Assert the returned status fields, `DEL: Cancel` hint, exact canceled-not-loaded versus canceled-preview labels, and removal of loading text from the final current frame.

- [ ] **Step 2: Write failing buffer/generation tests**

  Record fake GraphX calls and buffer pixels. Prove `ce_ui_refresh` updates both buffers without restarting the scheduler, coarse swaps only after a complete pass, fine swaps only after a complete pass, cancellation occurs between row steps, and an older generation cannot swap after a newer full draw/cancel.

- [ ] **Step 3: Run the host suite and confirm RED**

  Expected: new state/status APIs are missing and current full redraw behavior restarts rendering.

- [ ] **Step 4: Extend the existing `ce_ui.c` state machine**

  Keep the current `ProjectPlot2D render_job`; add only status, completed/total queries, a monotonically increasing generation counter, and the manual-reload flag. Draw progress with short labels that fit the 320-pixel header. Stop only at the existing one-row step boundary. Never label coarse pixels current.

- [ ] **Step 5: Add the partial refresh path**

  Reuse existing expression/header drawing helpers. Select each GraphX buffer in turn and redraw only the non-graph region plus status; restore the previously visible buffer. In Split, never clear or redraw the graph rectangle during this path.

- [ ] **Step 6: Run all host tests and commit**

  ```powershell
  git add src/platform/ce/ce_ui.* src/main.c tests/ce_ui_test.c tests/fake_ce.*
  git commit -m "feat: expose cancelable graph render states"
  ```

## Task 7: Route Input Before Drawing and Reload Only When Needed

**Files:**

- Modify: `src/app/app.h`, `src/app/app.c`
- Modify: `src/platform/ce/ce_input.c`
- Modify: `src/platform/ce/ce_ui.c`
- Modify: `src/main.c`
- Add: `tests/main_loop_test.c` (include `src/main.c` under a renamed entry point, following the existing source-instrumentation test pattern)
- Modify: `tests/test_main.c`, `tests/app_state_test.c`, `tests/ce_input_test.c`, `tests/ce_ui_test.c`

**Interfaces:**

- Add actions `APP_ACTION_TOGGLE_VISIBILITY` and `APP_ACTION_CANCEL_RENDER`.
- Preserve `app_handle_action`'s current running/quit contract. Add only `bool app_action_changes_graph(AppAction action)` for operation kinds that can change mathematical content or viewport; main applies it only after a successful operation and handles view/layout changes separately.
- Map not-editing `2nd`+`ENTER` to visibility and not-editing `DEL` to cancel. Preserve editing meanings.

- [ ] **Step 1: Write failing action-classification tests**

  Assert pan, zoom, 2D/3D changes, commit, clear, load, new, and visibility actions are graph-changing kinds; selection, editing, cancel, menu, save, view selection, and quit are not. Main must still require the underlying commit/clear/load/new/toggle operation to succeed. Assert invalid visibility toggles and unavailable cancels are no-ops.

- [ ] **Step 2: Write failing CE input tests**

  Verify `2nd`+`ENTER` only outside editing, `DEL` cancels only outside editing, editing `DEL` still deletes one character, and current top-row view keys remain reachable. Include a menu-open sample to prove background movement does not occur.

- [ ] **Step 3: Write failing UI integration tests**

  Type and move the cursor during an active render, change row selection, re-press a current page key, hit a selection boundary, and perform a failed commit: scheduler generation and completed-row count must not reset. Toggle visibility, successfully commit/clear, pan, zoom, change mode, New, Load, or switch Graph/Split layout: invalidate exactly once. After cancel, the already-active `GRAPH`/`WINDOW` key reloads; switching to Expressions does not; returning from Expressions to Graph/Split, New, and successful Load do reload as specified. Opening a modal cancels overwritten work and returning restarts it unless manual cancellation is latched.

- [ ] **Step 4: Write the failing poll-first loop test**

  In `tests/main_loop_test.c`, rename and include the real `src/main.c`, substitute observed input/draw/render functions, and run a short scripted loop. Assert every iteration records input handling before a full draw or render row, and at most one graph row occurs after higher-priority UI work. This adds no production hook.

- [ ] **Step 5: Run the host suite and confirm RED**

  Expected: actions and redraw routing do not yet distinguish UI from graph work.

- [ ] **Step 6: Reorder and simplify the main loop**

  Poll and fully handle one input action first. Track only `ui_dirty`, `graph_dirty`, and the render status already owned by `ce_ui`; do not add another cache or controller. Use `ce_ui_refresh` for editing/cursor/selection/status changes, `ce_ui_draw(..., true)` for authorized graph reloads, `ce_ui_draw(..., false)` for canceled/out-of-date surfaces, and one `ce_ui_render_step` last. Call `ce_input_discard_pending()` at every modal/new/load context boundary.

- [ ] **Step 7: Draw visibility and stale-state cues**

  Use each line's existing slot color for a filled visible marker and a hollow/muted hidden marker. Definitions/scalars get no marker. Keep labels readable in selected blue rows. Show “Canceled — GRAPH/WINDOW to load”, “Out of date — GRAPH/WINDOW to load”, and “Preview” only when true.

- [ ] **Step 8: Run host tests, build CE, and commit**

  ```powershell
  git add src/app src/platform/ce src/main.c tests/test_main.c tests/main_loop_test.c tests/app_state_test.c tests/ce_input_test.c tests/ce_ui_test.c
  git commit -m "feat: keep graph work across ui input"
  ```

## Task 8: Add the Private Diagnostics Page

**Files:**

- Modify: `src/platform/ce/ce_ui.h`, `src/platform/ce/ce_ui.c`
- Modify: `tests/fake_ce.h`, `tests/fake_ce.c`
- Modify: `tests/ce_ui_test.c`

**Interface and content:**

- Add one menu item and an internal `ce_ui_show_diagnostics()` page; selecting it displays the page modally and returns `APP_ACTION_NONE`, so no new application action is needed.
- Report measured free RAM, last completed coarse time, last completed fine time, longest row time, render restarts, user cancels, last completed drawable-equation count, and current freshness state.
- Read free RAM from the CE OS at page-open time. Measure stages/rows with the platform clock already available in CE C. Host fakes supply deterministic clock ticks and RAM.

- [ ] **Step 1: Extend fakes and write failing metrics tests**

  Feed deterministic clock increments and RAM values. Complete coarse/fine work, restart once, cancel once, then assert exact displayed values and freshness text. Verify opening and closing diagnostics does not move the viewport or alter project/save state.

- [ ] **Step 2: Run the host suite and confirm RED**

  Expected: the diagnostics item/page and counters do not exist.

- [ ] **Step 3: Measure inside existing render operations**

  Record start/end around coarse/fine stages and each row step in `ce_ui.c`; update fixed-size integer counters only. Saturate counters rather than wrapping. Query OS free RAM only when drawing diagnostics. Do not allocate history, logs, strings, or samples.

- [ ] **Step 4: Add the read-only modal page**

  Reuse the existing menu/dialog draw and key-wait pattern so keys cannot affect the background. On return, redraw the prior view without violating manual-cancel state.

- [ ] **Step 5: Run host tests, build CE, and commit**

  ```powershell
  git add src/platform/ce tests/fake_ce.* tests/ce_ui_test.c
  git commit -m "feat: add private render diagnostics"
  ```

## Task 9: Integrate, Measure, and Prepare the Private Hardware Build

**Files:**

- Modify: `README.md`
- Modify if verification exposes a defect: only files already in Tasks 1–8, with a regression test first
- Inspect: `bin/DGCALC.map`
- Produce privately: `bin/DGCALC.8xp`

- [ ] **Step 1: Run the complete host suite from a clean build state**

  Run:

  ```powershell
  $env:HOST_CC='C:\Users\Adrian\Documents\CHAT GPT PROJECTS\.toolchains\zig-x86_64-windows-0.17.0\zig.exe'
  powershell -ExecutionPolicy Bypass -File tools/test-host.ps1
  ```

  Expected: every check passes with zero compiler warnings and zero failures. Record the final assertion count.

- [ ] **Step 2: Build the calculator artifact twice**

  Set `CEDEV_HOME` to `C:\CEdev` and run `tools/build-ce.ps1` twice from unchanged sources. Record `bin/DGCALC.8xp` size and SHA-256; hashes must match. Inspect `bin/DGCALC.map` for the pending-key bytes, visibility mask, render controller/diagnostics state, total BSS, heap remaining, and static screen buffers. Confirm no third full-screen allocation and no per-expression render cache exists.

- [ ] **Step 3: Update the private README checklist**

  Document the exact controls, relation cycle, inverse-trig names, visibility marker, three loading stages, cancellation/reload behavior, freshness labels, diagnostics fields, radians, and format-v3 migration. Mark paper math, catalog, intercept tools, inequalities, and 3D rendering as deferred. State clearly that this is a private test build, not a release.

- [ ] **Step 4: Perform the independent whole-branch review**

  Compare every item in the approved spec to code and tests. Review memory ownership, codec boundaries, modifier/pending-key clearing, manual-reload transitions, buffer selection, render-generation checks, modal isolation, and every no-op path. Fix only verified issues, each with a regression test.

- [ ] **Step 5: Commit documentation and final fixes**

  ```powershell
  git add README.md src tests
  git commit -m "docs: prepare milestone 2b private hardware test"
  ```

- [ ] **Step 6: Hand off for physical-calculator verification**

  Provide the private `.8xp` and hash only in this conversation. Ask the user to verify: fast `X` then `+`; same-scan modifiers; edit responsiveness during loading; all progress stages; DEL cancel; explicit reload; visibility; relation cycling; inverse trig; menu/modal isolation; reset/reopen; version 2 save migration; and diagnostics after a line, vertical line, circle, ellipse, rotated ellipse, and `y=1/x`. Record free RAM and recovery after repeated edit, toggle, cancel, reload, clear, save, and load cycles. Do not claim physical performance, RAM safety, or final approval until those checks are reported.

## Completion Gate

Implementation is complete only when:

- all host tests pass;
- two clean CE builds are byte-identical;
- map inspection confirms the two-buffer design and expected memory bounds;
- independent specification and quality reviews pass;
- the private artifact is handed off with its hash;
- physical behavior remains explicitly “awaiting user verification” until tested on the TI-84 Plus CE Python;
- no shareable/public release artifact, upload, push, merge, or publication has been created.
