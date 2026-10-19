# Milestone 2A: 2D Equality Plotting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add private, memory-safe 2D equality plotting with implicit curves, multiple equations, pan/zoom, progressive refinement, and backward-compatible saved viewports to DGCALC.

**Architecture:** Keep parsing in the existing fixed scratch arena, but expose read-only AST views so committed expressions can own right-sized heap payloads. Feed coordinate-aware project evaluation into a row-batched marching-squares plotter, then let a small project scheduler and the CE UI progressively draw coarse and refined frames while input remains responsive.

**Tech Stack:** C11; CEdev v15.0; CE libraries `graphx`, `keypadc`, and `fileioc`; existing PowerShell host-test and calculator-build scripts; no new dependency or assembly.

**Spec:** `docs/superpowers/specs/2026-10-19-milestone-2a-2d-equality-plotting-design.md`

## Global Constraints

- Target the user's TI-84 Plus CE Python running arTIfiCE Shell 2.1 with approximately 117,000 bytes of free RAM.
- Optimize the normal case of five or fewer equations while retaining all 16 expression positions.
- A blank expression position may keep a small descriptor but must not reserve source, AST, sample-grid, or drawing payloads.
- Keep `EDITOR_CAPACITY=256` and `AST_MAX_NODES=128` per expression.
- Use 8-pixel coarse sampling and 4-pixel refined sampling.
- `Y=`, `WINDOW`, `ZOOM`, `TRACE`, and `GRAPH` retain their approved top-key behavior.
- Full Graph arrows pan, `+` and `-` zoom, and `CLEAR` cannot erase a hidden expression.
- Viewport-only changes do not mark mathematical content dirty.
- Existing version 1 projects must load with the default viewport; version 2 saves include the viewport.
- Allocation, parsing, evaluation, rendering, and loading failures must preserve the previous valid expression or project.
- Use ordinary C first; add no assembly or dependency.
- Create private test `.8xp` files only. Do not publish, upload, release, or create a public/shareable package.

## Review Focus

- Allocation failure while replacing an expression or decoding a project must leave the old payload, dependencies, state, and save intact; pinned in Tasks 3 and 5.
- A sign change across a pole such as `y=1/x` must not become a false contour segment; pinned in Task 7.
- Truncated version 1 data, invalid version 2 viewport floats, unknown versions, and CRC damage must be rejected transactionally; pinned in Task 5.
- Repeated pan/zoom input or opening a modal menu during refinement must cancel stale work without moving the graph behind the menu; pinned in Tasks 4 and 9.
- Blank projects, five typical equations, 16 populated lines, and repeated clear/re-edit cycles must stay bounded and release every expression payload; pinned in Tasks 3 and 8.

---

## Planned File Structure

```text
src/math/ast.h, ast.c                 Fixed parse arena plus read-only AstView
src/math/eval.h, eval.c               Evaluation over AstView
src/math/deps.h, deps.c               Dependency scans over AstView
src/math/validate.h, validate.c       Validation over AstView
src/project/project.h, project.c      Sparse expression ownership, editing, coordinate evaluation
src/graph/viewport.h, viewport.c      Viewport mapping, pan/zoom, adaptive grid spacing
src/graph/plot2d.h, plot2d.c          One-expression row-batched marching squares
src/graph/project_plot2d.h/.c          Multi-expression scheduling with shared buffers
src/app/app.h, app.c                  Viewport state and navigation actions
src/storage/project_codec.h/.c        Version 1 reader and version 2 reader/writer
src/storage/atomic_store.h/.c         Transactional load with memory-error reporting
src/platform/ce/ce_input.h/.c         View-sensitive Graph controls
src/platform/ce/ce_ui.h/.c            Grid, axes, progressive GraphX drawing
src/main.c                             Editing lifecycle, render invalidation, cleanup
tests/fake_ce.h, fake_ce.c             Shared GraphX/keypad host fakes
tests/ast_view_test coverage           Added to existing math tests
tests/project_test.c                   Sparse ownership and failure tests
tests/viewport_test.c                  Mapping, navigation, and grid tests
tests/ce_input_test.c                  Graph-only key routing tests
tests/project_codec_test.c             Version migration and viewport tests
tests/plot2d_test.c                    Contour geometry and discontinuity tests
tests/project_plot2d_test.c            Multiple-line scheduling and memory bounds
tests/ce_ui_test.c                     Progressive GraphX and modal behavior tests
tools/test-host.ps1                    Include CE input and new portable modules
README.md                              Private Milestone 2A behavior and test checklist
```

## Task 1: Read-Only AST Views

**Files:**

- Modify: `src/math/ast.h`
- Modify: `src/math/ast.c`
- Modify: `src/math/eval.h`
- Modify: `src/math/eval.c`
- Modify: `src/math/deps.h`
- Modify: `src/math/deps.c`
- Modify: `src/math/validate.h`
- Modify: `src/math/validate.c`
- Modify: `src/project/project.c`
- Modify: `src/storage/project_codec.c`
- Modify: `tests/eval_test.c`
- Modify: `tests/parser_test.c`

**Interfaces:**

- Produces: `AstView { const AstNode *nodes; uint16_t count; }`
- Produces: `AstView ast_arena_view(const AstArena *arena)`
- Changes consumers to `expression_eval(AstView tree, AstId root, const EvalContext *context)`, `expression_validate(AstView tree, AstId root)`, `expression_variable_mask(AstView tree, AstId root)`, and `dependency_scan(AstView tree, AstId root, ...)`
- Preserves: `parser_parse(const char *, AstArena *)`; parsing still uses the fixed 128-node scratch arena

- [ ] **Step 1: Write the failing compact-view test**

Add `test_evaluator_accepts_a_compact_ast_view()` in `tests/eval_test.c`. Parse `x^2+y^2=25`, copy exactly `arena.count` nodes into a smaller test array, construct `AstView`, resolve `x=3` and `y=4`, and assert residual `0.0f`. Also run validation and variable-mask checks through the view.

- [ ] **Step 2: Run the host suite and verify the new API is missing**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails because `AstView` and the view-based signatures do not exist.

- [ ] **Step 3: Add `AstView` and migrate read-only tree walkers**

Keep `AstArena` unchanged for parsing. Convert evaluation, validation, variable scanning, dependency scanning, project calls, codec validation, and their tests to pass `ast_arena_view(&arena)` or another `AstView`. A view with `nodes=NULL`, `count=0`, or an out-of-range root returns the same existing validation/evaluation failure as an invalid arena.

- [ ] **Step 4: Run all host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: all existing tests plus the compact-view test pass with zero warnings.

- [ ] **Step 5: Commit**

```powershell
git add src/math src/project/project.c src/storage/project_codec.c tests
git commit -m "refactor: read math trees through compact views"
```

## Task 2: Encapsulate Project Editing and Line Access

**Files:**

- Modify: `src/project/project.h`
- Modify: `src/project/project.c`
- Modify: `src/main.c`
- Modify: `src/platform/ce/ce_ui.c`
- Modify: `src/storage/project_codec.c`
- Modify: `tests/app_state_test.c`
- Modify: `tests/project_test.c`
- Modify: `tests/project_codec_test.c`
- Modify: `tests/atomic_store_test.c`
- Modify: `tests/ce_ui_test.c`

**Interfaces:**

- Produces: `bool project_begin_edit(Project *project, uint8_t index)`
- Produces: `void project_cancel_edit(Project *project)`
- Produces: `bool project_is_editing(const Project *project)` and `uint8_t project_editing_index(const Project *project)`
- Produces: `EditorBuffer *project_editor(Project *project)`
- Produces: `const char *project_line_source(const Project *project, uint8_t index)`
- Produces: `const char *project_line_display_text(const Project *project, uint8_t index)`
- Produces: `bool project_line_has_committed(...)`, `ExpressionStatus project_line_status(...)`, `ExprRole project_line_role(...)`, and `bool project_line_tree(..., AstView *tree, AstId *root)`
- Preserves: `project_set_draft`, `project_validate_draft`, `project_commit_line`, `project_clear_line`, and scalar evaluation behavior

- [ ] **Step 1: Write failing editing-lifecycle tests**

Add tests that begin editing `a=2`, change the draft to `a=`, cancel, and assert that the source, status, count, and pre-edit dirty flag are restored. Add a test that a failed commit keeps the draft visible while the committed source and dependent evaluation remain valid.

- [ ] **Step 2: Run the host suite and verify the lifecycle API is missing**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on `project_begin_edit` and the accessors.

- [ ] **Step 3: Implement the project access boundary**

Move backup/count/dirty edit cancellation from `src/main.c` into `Project`. Keep current fixed storage for this task. Update main, UI, codec, and every test to use the accessors instead of reading, assigning, or comparing `project.lines[]` directly. Replace shallow `Project` assignment and `memcmp` assertions with semantic source/status/evaluation assertions or encoded-byte snapshots.

- [ ] **Step 4: Verify there are no external line-layout dependencies**

Run:

```powershell
rg -n "\.lines\[|= project_before|project_before =" src tests -g "*.c"
```

Expected: direct line storage access remains only inside `src/project/project.c`; no `Project` is shallow-copied.

- [ ] **Step 5: Run all host tests and the CE build**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Run: `powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1`

Expected: both pass; editing, menus, saving, and loading retain Milestone 1 behavior.

- [ ] **Step 6: Commit**

```powershell
git add src tests
git commit -m "refactor: encapsulate project editing state"
```

## Task 3: Sparse Expression Payloads

**Files:**

- Modify: `src/project/project.h`
- Modify: `src/project/project.c`
- Modify: `src/storage/project_codec.h`
- Modify: `src/storage/project_codec.c`
- Modify: `src/storage/atomic_store.h`
- Modify: `src/storage/atomic_store.c`
- Modify: `src/main.c`
- Modify: `tests/project_test.c`
- Modify: `tests/project_codec_test.c`
- Modify: `tests/atomic_store_test.c`
- Modify: remaining tests that create `Project` values

**Interfaces:**

- Produces: `ProjectMemory { void *(*allocate)(size_t); void (*release)(void *); }`
- Produces: `void project_init_with_memory(Project *project, ProjectMemory memory)`; `project_init` uses `malloc` and `free`
- Produces: `void project_dispose(Project *project)`
- Produces: `size_t project_payload_bytes(const Project *project)`
- Produces: `void project_replace(Project *destination, Project *staging)`; ownership moves without copying payload pointers
- Adds: `bool out_of_memory` to `CommitResult`
- Adds: `CODEC_ERR_MEMORY` and `STORE_ERR_MEMORY` so the UI can show `Memory is insufficient`

- [ ] **Step 1: Write failing allocation-accounting tests**

Use a counting allocator in `tests/project_test.c`. Assert that a blank 16-slot project has `project_payload_bytes()==0`, committing five expressions creates five live allocations, clearing them returns to zero, and 50 edit/clear cycles leave zero live allocations after `project_dispose`.

- [ ] **Step 2: Write the failing replacement-allocation test**

Commit `x^2+y^2=25`, configure the allocator to fail the next allocation, attempt to commit `9x^2+4y^2=36`, and assert `out_of_memory`, the old source/tree/evaluation remain intact, and live allocation totals do not change.

- [ ] **Step 3: Run the focused project tests and verify failure**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on the memory API.

- [ ] **Step 4: Implement one right-sized allocation per committed line**

Each nonblank descriptor owns one payload containing its header, exactly `node_count` `AstNode` values, and the null-terminated committed source. Parse into the existing static scratch arena, allocate and populate the replacement, then swap and free the old payload only after validation succeeds. Keep one shared `EditorBuffer` and pre-edit bookkeeping in `Project`; inactive lines retain no draft buffer.

- [ ] **Step 5: Make destruction and replacement explicit**

Dispose projects before New and program exit. Decode loads into a staging project that inherits the destination allocator, then moves ownership only on complete success. Update every test fixture to dispose or replace initialized projects rather than reinitializing or shallow-copying live pointers.

- [ ] **Step 6: Run memory, storage, and full host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: zero failures, zero warnings, zero counting-allocator leaks, and the injected allocation failure preserves existing content.

- [ ] **Step 7: Build for CEdev and record static sizes**

Run: `powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1`

Expected: CE build passes. Record `sizeof(Project)`, blank payload bytes, and five-expression payload bytes in the task notes; five typical equations plus plotting buffers must remain below the approved 20 KB project/renderer target.

- [ ] **Step 8: Commit**

```powershell
git add src tests
git commit -m "refactor: store expression payloads sparsely"
```

## Task 4: Viewport, Adaptive Grid, and Graph-Only Input

**Files:**

- Create: `src/graph/viewport.h`
- Create: `src/graph/viewport.c`
- Create: `tests/viewport_test.c`
- Create: `tests/ce_input_test.c`
- Create: `tests/fake_ce.h`
- Create: `tests/fake_ce.c`
- Modify: `src/app/app.h`
- Modify: `src/app/app.c`
- Modify: `src/platform/ce/ce_input.h`
- Modify: `src/platform/ce/ce_input.c`
- Modify: `tests/app_state_test.c`
- Modify: `tests/ce_ui_test.c`
- Modify: `tests/fakes/keypadc.h`
- Modify: `tests/test.h`
- Modify: `tests/test_main.c`
- Modify: `tools/test-host.ps1`

**Interfaces:**

- Produces: `Viewport2D { float center_x; float center_y; float units_per_pixel; }`
- Produces: `GraphRect { int16_t x; int16_t y; int16_t width; int16_t height; }`
- Produces: `viewport2d_init`, pixel/world conversion functions, `viewport2d_pan_pixels`, `viewport2d_zoom`, and `viewport2d_grid_step`
- Adds to `AppState`: `Viewport2D viewport`
- Adds actions: `APP_ACTION_PAN_LEFT`, `PAN_RIGHT`, `PAN_UP`, `PAN_DOWN`, `ZOOM_IN`, and `ZOOM_OUT`
- Changes: `ce_input_poll(EditorBuffer *editor, bool editing, AppView view)`
- Produces: `bool ce_input_graph_navigation_active(void)`

- [ ] **Step 1: Write failing viewport tests**

Assert the default 320-pixel graph spans approximately `[-10,10]`, screen/world round trips are within `0.0001f`, each pan changes the center by 10 percent of the visible dimension, zoom uses factor `1.25f`, and scale clamps to finite values in `[0.0001f,10000.0f]`. Assert adaptive grid steps follow the `1,2,5 × 10^n` sequence and stay between 20 and 50 pixels where the clamp allows.

- [ ] **Step 2: Write failing view-sensitive input tests**

Move existing GraphX/keypad globals and stubs from `ce_ui_test.c` into the shared fake. Script keys to assert: Split `UP/DOWN` return selection actions; Graph arrows return pan actions; Graph `+/-` return zoom actions; Graph `CLEAR` returns `APP_ACTION_NONE`; Expressions retain clear/edit behavior; top keys and `2nd`+`MODE` remain unchanged.

- [ ] **Step 3: Run the host suite and verify missing APIs**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on viewport and graph-navigation symbols.

- [ ] **Step 4: Implement viewport math and application actions**

Use default `units_per_pixel=20.0f/320.0f`. In full Graph only, horizontal pan uses 32 pixels and vertical pan uses 19.6 pixels; screen-up increases world `center_y`. Zoom-in divides scale by 1.25 and zoom-out multiplies it. None of these actions alters project dirty state.

- [ ] **Step 5: Implement view-sensitive keypad routing**

Pass the active view into `ce_input_poll`. When not editing in Graph, route arrows and `+/-` to navigation, ignore `CLEAR`, and leave all top-key/menu/quit shortcuts active. Keep Split and Expressions selection behavior. `ce_input_graph_navigation_active` inspects only the four arrows and `+/-`.

- [ ] **Step 6: Run host tests and CE build**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Run: `powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1`

Expected: both pass; existing menu tests still prove modal dialogs restore continuous keypad mode.

- [ ] **Step 7: Commit**

```powershell
git add src/app src/graph src/platform/ce/ce_input.* tests tools/test-host.ps1
git commit -m "feat: add 2d viewport navigation"
```

## Task 5: Version 2 Saved Viewports and Version 1 Migration

**Files:**

- Modify: `src/storage/project_codec.h`
- Modify: `src/storage/project_codec.c`
- Modify: `src/storage/atomic_store.c`
- Modify: `src/main.c`
- Modify: `tests/project_codec_test.c`
- Modify: `tests/atomic_store_test.c`

**Interfaces:**

- Changes: `PROJECT_FORMAT_VERSION` from `1` to `2`
- Defines: version 1 metadata is 4 bytes; version 2 metadata is 16 bytes
- Version 2 metadata order: `mode`, `view`, `selected`, `count`, then little-endian IEEE-754 `center_x`, `center_y`, and `units_per_pixel`
- Preserves the existing per-line length-prefixed source payload and 13-byte magic/version/length/CRC header
- Accepts versions 1 and 2; writes only version 2

- [ ] **Step 1: Write failing version-migration tests**

Add a checked-in test helper that constructs valid version 1 bytes and CRC without using the current encoder. Assert it loads expressions and receives the default viewport. Add version 2 round-trip assertions for nondefault center/scale and selection.

Set `project.dirty=false`, change only the viewport, explicitly encode/save, and assert the nondefault viewport still round trips without creating a dirty-content warning requirement.

- [ ] **Step 2: Write failing viewport-corruption and allocation-failure tests**

Mutate version 2 viewport bytes to NaN, infinity, zero scale, and negative scale, recompute CRC, and assert `CODEC_ERR_CONTENT` without destination mutation. Inject allocation failure during decode and assert `CODEC_ERR_MEMORY`, zero staging leaks, and unchanged destination expressions and state.

- [ ] **Step 3: Run codec tests and verify failure**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: version 1 is rejected and viewport fields do not round trip yet.

- [ ] **Step 4: Implement dual-version validation and version 2 encoding**

Select metadata length from the input version before payload validation. Decode floats through `uint32_t` plus `memcpy`, reject non-finite centers and non-finite/nonpositive/out-of-clamp scale, and default version 1 viewports. Validate the entire byte document before allocating; decode into a staging project and replace only after every expression succeeds.

- [ ] **Step 5: Preserve the one active draft without fixed per-line buffers**

Version 2 writes the committed source for every line and a differing draft only for the active selected edit. On load, committed sources are restored first and only that selected differing draft reopens editing. For version 1, preserve the selected line's differing draft and committed content for every line; ignore impossible extra inactive drafts rather than allocating duplicate editors.

- [ ] **Step 6: Run codec, atomic-store, fuzz, and full tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: version 1 and 2 valid projects load; all corruption, fuzz, memory, and atomic-save failures remain transactional.

- [ ] **Step 7: Commit**

```powershell
git add src/storage src/main.c tests/project_codec_test.c tests/atomic_store_test.c
git commit -m "feat: save 2d viewport with v1 compatibility"
```

## Task 6: Coordinate-Aware Project Evaluation

**Files:**

- Modify: `src/project/project.h`
- Modify: `src/project/project.c`
- Modify: `tests/project_test.c`

**Interfaces:**

- Produces: `EvalResult project_evaluate_2d(const Project *project, uint8_t index, float x, float y)`
- Produces: `bool project_line_is_drawable_2d(const Project *project, uint8_t index)`
- Preserves: `project_evaluate_line` for scalar expressions and definitions

- [ ] **Step 1: Write failing coordinate-evaluation tests**

Assert residuals for `y=x` at `(2,2)`, `x=2` at `(2,9)`, `x^2+y^2=25` at `(3,4)`, and `25=x^2+y^2` at `(3,4)`. Add `a=2`, `f(t)=t^2+a`, `y=f(x)` and assert `(3,11)` is zero. Assert inequalities, invalid/undefined lines, and a `z` relation are not drawable.

- [ ] **Step 2: Run project tests and verify the API is missing**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on `project_evaluate_2d`.

- [ ] **Step 3: Add coordinate bindings to the existing dependency evaluator**

Extend `ProjectEvalState` with optional `x/y` bindings. Local function parameters take precedence, then coordinates, then user definitions. A 2D call accepts only a valid `EXPR_ROLE_RELATION_2D` equality; `z`, undefined names, domain errors, division by zero, and non-finite results return their existing `EvalError`.

- [ ] **Step 4: Run all host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: all scalar, definition, cycle, and new coordinate tests pass.

- [ ] **Step 5: Commit**

```powershell
git add src/project tests/project_test.c
git commit -m "feat: evaluate relations at 2d coordinates"
```

## Task 7: Row-Batched Marching Squares

**Files:**

- Create: `src/graph/plot2d.h`
- Create: `src/graph/plot2d.c`
- Create: `tests/plot2d_test.c`
- Modify: `tests/test_main.c`

**Interfaces:**

- Defines: `PLOT2D_COARSE_STEP=8`, `PLOT2D_FINE_STEP=4`, and `PLOT2D_MAX_COLUMNS=81`
- Produces: `typedef bool (*Plot2DSampleFn)(void *user, float x, float y, float *residual)`
- Produces: `typedef void (*Plot2DSegmentFn)(void *user, int16_t x0, int16_t y0, int16_t x1, int16_t y1)`
- Produces: fixed-size `Plot2DJob` containing two residual rows, two validity rows, viewport/rectangle, grid progress, and no heap pointer
- Produces: `bool plot2d_begin(Plot2DJob *, Viewport2D, GraphRect, uint8_t step)`
- Produces: `Plot2DStep plot2d_step(Plot2DJob *, Plot2DSampleFn, void *, Plot2DSegmentFn, void *)`, processing at most one cell row

- [ ] **Step 1: Write failing line, circle, and reversed-equation tests**

Use analytic residual callbacks. Assert a diagonal and vertical line emit in-bounds segments, circle and ellipse segment midpoints remain near their expected contours, and multiplying or reversing a residual's sign produces equivalent segment bounds.

- [ ] **Step 2: Write failing ambiguity, invalid-gap, and pole tests**

Assert cases 5 and 10 choose connections from a center sample. Return invalid samples inside a band and assert no segment crosses it. For `F(x,y)=y-1/x`, use a rectangle whose cell straddles `x=0` and assert no emitted segment bridges the pole.

- [ ] **Step 3: Run plotter tests and verify the module is missing**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on `graph/plot2d.h`.

- [ ] **Step 4: Implement bounded row sampling and marching squares**

Accept only 4- or 8-pixel steps and rectangles producing at most 81 columns. Sample the first row once, then one new row per step and reuse the older row. Place the last sample on the rectangle's final pixel even when dimensions are not divisible by the step. Interpolate edge crossings, resolve ambiguous cells by the center sign, and emit only rectangle-clipped integer segments.

- [ ] **Step 5: Add discontinuity validation**

Reject any cell with an invalid/non-finite corner. For each sign-changing edge, perform up to three finite bisection samples: accept the crossing only when an exact zero is found or the smallest sampled residual magnitude moves toward zero; reject an invalid midpoint or a sign change whose residual magnitudes only grow toward a pole. Also require the proposed segment midpoint to be finite and no larger in residual magnitude than the cell's largest corner. This preserves ordinary curved contours while rejecting the tested asymptote bridge.

- [ ] **Step 6: Run all host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: geometry, discontinuity, and all prior tests pass with zero warnings.

- [ ] **Step 7: Commit**

```powershell
git add src/graph/plot2d.* tests/plot2d_test.c tests/test_main.c
git commit -m "feat: extract 2d equality contours"
```

## Task 8: Multiple-Expression Render Scheduler

**Files:**

- Create: `src/graph/project_plot2d.h`
- Create: `src/graph/project_plot2d.c`
- Create: `tests/project_plot2d_test.c`
- Modify: `tests/test_main.c`

**Interfaces:**

- Produces: fixed-size `ProjectPlot2D` owning one `Plot2DJob`, current expression index, rectangle, viewport, and step
- Produces: `void project_plot2d_begin(ProjectPlot2D *, Viewport2D, GraphRect, uint8_t step)`
- Produces: `ProjectPlot2DStep project_plot2d_step(ProjectPlot2D *, const Project *, ProjectPlot2DSegmentFn, void *)`
- Segment callback includes the expression slot index before pixel endpoints so CE can select its automatic color
- Produces: `void project_plot2d_cancel(ProjectPlot2D *)`

- [ ] **Step 1: Write failing multiple-line scheduler tests**

Create a project with definitions, blanks, an invalid draft, 2D equalities, and a 3D equality. Assert only committed drawable 2D equalities emit segments, each callback carries the correct slot index, and repeated calls process at most one plot row.

- [ ] **Step 2: Add blank, five-line, and 16-line memory tests**

Assert a blank project reaches done without invoking evaluation or allocating payloads. Run five typical equations and then 16 simple equations through coarse and fine jobs; assert both complete, scheduler size is fixed, and `project_payload_bytes` returns to zero after clearing and disposal.

- [ ] **Step 3: Run scheduler tests and verify the module is missing**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on `graph/project_plot2d.h`.

- [ ] **Step 4: Implement the shared-buffer scheduler**

Scan slots in order, skip nondrawable lines without starting a job, bridge samples to `project_evaluate_2d`, and reuse the same `Plot2DJob` for every expression. A cancel clears only scheduler progress and never mutates the project.

- [ ] **Step 5: Run all host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: all 0/5/16-line tests and prior tests pass.

- [ ] **Step 6: Commit**

```powershell
git add src/graph/project_plot2d.* tests/project_plot2d_test.c tests/test_main.c
git commit -m "feat: schedule multiple 2d equations"
```

## Task 9: Progressive Calculator Rendering and Private Build

**Files:**

- Modify: `src/platform/ce/ce_ui.h`
- Modify: `src/platform/ce/ce_ui.c`
- Modify: `src/main.c`
- Modify: `tests/fake_ce.c`
- Modify: `tests/fake_ce.h`
- Modify: `tests/fakes/graphx.h`
- Modify: `tests/ce_ui_test.c`
- Modify: `README.md`

**Interfaces:**

- Preserves: `ce_ui_draw(const AppState *, const Project *, bool editing)` for immediate static redraw and render invalidation
- Produces: `bool ce_ui_render_step(const AppState *, const Project *, bool navigation_active)`; performs at most one scheduler row and returns whether work remains
- Keeps the project menu and dirty/message dialogs synchronous and modal

- [ ] **Step 1: Write failing adaptive-grid and graph-segment UI tests**

Extend the GraphX fake to record grid lines, segment lines, colors, and swaps. Assert Graph and Split use viewport-aware grid positions, visible zero axes are emphasized, integer labels stay inside the graph rectangle, and two equations draw with different fixed palette colors.

- [ ] **Step 2: Write failing progressive/cancellation tests**

Assert `ce_ui_draw` starts an 8-pixel coarse job, `ce_ui_render_step` advances one row, coarse completion swaps once, and idle input starts a 4-pixel fine job. Change the viewport before fine completion and assert old work is discarded. Script opening and closing the menu during pending work and assert neither viewport nor background moves.

- [ ] **Step 3: Run host UI tests and verify progressive API failure**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on `ce_ui_render_step`.

- [ ] **Step 4: Replace the placeholder with viewport-aware grid and axes**

Draw `1,2,5 × 10^n` grid lines from `viewport2d_grid_step`, emphasize visible zero axes, and print integer tick labels only when they fit inside the graph panel. Retain the current 3D placeholder when mode is 3D.

- [ ] **Step 5: Integrate coarse and refined double-buffer frames**

On invalidation, cancel old work, draw and show a static empty-grid frame immediately, redraw the same base into the hidden buffer, and begin coarse scheduling. Swap only when the coarse frame completes; then rebuild the hidden base and begin fine scheduling after navigation keys are released. New input cancels the hidden frame and restarts from current state.

- [ ] **Step 6: Update the main loop and memory errors**

Poll input before each render step. Any edit, view, mode, viewport, New, or Load change calls `ce_ui_draw`; an idle loop calls one `ce_ui_render_step`. Graph `CLEAR` remains ignored. Show `Memory is insufficient` on commit/load allocation failure, keep the editor open after failed commit, and dispose the project on New, successful replacement, and exit.

- [ ] **Step 7: Run the complete automated verification**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: zero failures and warnings, including menu, save recovery, sparse memory, migration, geometry, scheduler, and progressive UI tests.

Run: `powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1`

Expected: CEdev v15.0 succeeds and produces only the private `bin/DGCALC.8xp` development artifact.

- [ ] **Step 8: Update private documentation and inspect the branch**

Update README controls and scope, remove the “plotting later” statement for 2D equality mode, retain the no-sharing warning, and add the physical checklist: line, vertical line, circle, ellipse, reversed circle, `y=1/x`, pan/zoom, menu/quit, v1 load, v2 save/load, RAM readings at 0/1/5/16 equations, and RAM recovery after repeated clear/re-edit cycles.

Run:

```powershell
git diff --check
git status --short
git log --oneline --decorate -12
Get-FileHash .\bin\DGCALC.8xp -Algorithm SHA256
```

Expected: no whitespace errors or unintended files; private build path and SHA-256 are recorded for the user's calculator test.

- [ ] **Step 9: Commit**

```powershell
git add src tests README.md tools/test-host.ps1
git commit -m "feat: render private 2d equality graphs"
```

- [ ] **Step 10: Request physical-calculator verification**

Provide the private `.8xp` path and checklist to the user. Do not package, upload, publish, merge to a release branch, or call the milestone complete until the user reports the required hardware checks.
