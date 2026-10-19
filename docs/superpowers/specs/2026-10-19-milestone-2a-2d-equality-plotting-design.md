# Milestone 2A: 2D Equality Plotting Design

**Date:** 2026-10-19
**Status:** Approved in conversation; awaiting written-spec review

## Purpose

Add the first usable 2D graphing slice to the native TI-84 Plus CE Python application. It must plot ordinary and implicit equalities from the existing general expression list, including lines, vertical lines, circles, and ellipses, without requiring `y=` form.

This is the first of three private Milestone 2 builds. It establishes the shared 2D contour renderer, graph navigation, saved viewport, and sparse expression memory model. Later Milestone 2 builds will add inequalities and other graph classes, then interactive Desmos-style features.

## User Context and Constraints

- Target device: TI-84 Plus CE Python running arTIfiCE Shell 2.1.
- The calculator currently has approximately 117,000 bytes of free RAM.
- Normal use is expected to contain no more than five equations, but all 16 expression positions must remain available.
- Blank expression positions must not reserve full parse trees or graph buffers.
- The interface retains the familiar calculator layout already approved and tested.
- Development remains private. Private calculator test files are allowed, but no public or shareable package may be created without explicit final approval.

## Scope

### Included

- Plot multiple 2D equalities from the existing expression list.
- Accept both explicit and implicit forms, including:
  - `y=x`
  - `x=2`
  - `x=y^2`
  - `x^2+y^2=25`
  - `25=x^2+y^2`
  - `9x^2+4y^2=36`
- Pan and zoom in full Graph view.
- Draw a coarse graph during interaction and refine it when input becomes idle.
- Preserve the graph viewport in newly saved projects.
- Load existing version 1 projects with a default viewport.
- Store expression data sparsely so blank positions use only small descriptors.
- Fail safely on invalid expressions, undefined values, discontinuities, and allocation failure.

### Deferred

- Inequalities and filled regions
- Domain and range restrictions
- Polar and parametric curves
- Points, tables, lists, sliders, and animation
- Per-expression style editing
- Trace, intersections, roots, extrema, and numerical analysis
- 3D rendering

Definitions already accepted by the math foundation may be referenced by plotted equalities, but definitions are not themselves drawn.

## User Experience

### View Behavior

- `Y=` opens Expressions.
- `WINDOW` opens Compact Split.
- `ZOOM` toggles the existing 2D/3D project mode.
- `TRACE` opens or closes the project menu.
- `GRAPH` opens the full Graph view.

Compact Split remains expression-focused: arrow keys select expression lines and the graph pane is a live preview. Graph navigation occurs only in full Graph view, which avoids overloading the same arrow keys with selection and panning.

In full Graph view:

- Arrow keys pan the viewport.
- `+` zooms in.
- `-` zooms out.
- `CLEAR` does nothing, so it cannot erase an expression hidden by the full-screen graph.
- The existing `2nd`, then `MODE` quit sequence remains available.

Expression and Split views retain their existing editing, deletion, clearing, menu, and quit behavior.

### Viewport

The 2D viewport contains a center point and a positive units-per-pixel scale. The default full Graph view is centered at `(0,0)` and shows approximately `-10` through `10` horizontally. The vertical range follows the graph panel's aspect ratio.

Each arrow press pans by 10 percent of the visible width or height. Each zoom changes scale by a factor of 1.25 around the viewport center. Scale is clamped to a finite, positive range so repeated zooming cannot overflow or underflow calculator arithmetic. The exact clamp values may be adjusted during real-hardware calibration without changing the saved format.

Viewport changes do not mark mathematical project content dirty and therefore do not trigger an unsaved-changes warning by themselves. The latest viewport is included whenever the user explicitly saves the project.

### Grid and Axes

The placeholder's fixed grid is replaced with a viewport-aware grid in both Graph and Compact Split. Grid spacing uses a `1`, `2`, or `5` multiple of a power of ten so major lines remain roughly 20 to 50 pixels apart while zooming. The `x=0` and `y=0` axes are emphasized whenever they are visible. Compact numeric tick labels are drawn only when they fit without obscuring the expression panel or footer.

## Equality Rendering

### Unified Residual Model

Every drawable equality is evaluated as a residual:

`F(x,y) = left(x,y) - right(x,y)`

The renderer extracts the contour `F(x,y)=0`. This treats explicit, reversed, vertical, and implicit equalities uniformly. It avoids separate special cases for `y=f(x)`, circles, ellipses, and other relations.

A relation is drawable in this slice when it:

- is a valid equality,
- depends on `x`, `y`, or both,
- does not depend on `z`, and
- can resolve any referenced definitions without an evaluation error.

Scalar or function definitions provide values to other expressions but are not drawn. Non-equality relations and 3D relations remain stored and editable; the 2D renderer skips them.

### Marching Squares

The renderer samples the residual on a screen-aligned grid and uses marching squares to emit clipped line segments directly to the graph surface.

- Coarse grid spacing: 8 pixels.
- Refined grid spacing: 4 pixels.
- Samples are mapped through the current viewport rather than stored in world-space geometry.
- Two rows of sample values are reused for every expression.
- Segments are drawn as they are produced; the program does not retain a full segment list or a per-expression pixel buffer.
- Expressions are drawn in slot order with a small fixed color palette. User-editable styles remain deferred.

Ambiguous marching-squares cases use a residual sample at the cell center to select the connection. Candidate segments near very large value changes receive an additional residual check near the proposed crossing. A segment is rejected when evaluation is non-finite, undefined, or inconsistent with a local zero crossing. These checks prevent a false line from bridging an asymptote such as the discontinuity in `y=1/x`.

### Progressive Rendering

Panning or zooming immediately invalidates outdated graph work. While a navigation key is active, only the 8-pixel coarse graph is requested. Once navigation input is idle, the renderer replaces it with the 4-pixel refined graph.

Sampling proceeds in row-sized batches and polls input between batches. New navigation or view input cancels unfinished work before another batch begins. Invalidating a render does not mutate expressions or saved project data.

Empty and skipped lines require no sampling. All 16 populated lines remain supported, but performance calibration focuses on the expected case of five or fewer plotted equations.

## Architecture and Data Flow

### Pure 2D Plotter

A calculator-independent plotter owns coordinate conversion, residual sampling, marching-squares decisions, discontinuity checks, and segment clipping. It receives:

- a graph rectangle,
- a viewport,
- one validated expression,
- a requested coarse or refined step, and
- a segment callback.

It does not call GraphX, read keypad state, save projects, or modify expressions. Host tests can therefore capture and inspect emitted segments.

### Coordinate-Aware Evaluation

The existing evaluator gains a coordinate-aware entry point that resolves `x` and `y` from the sample location while retaining the existing definition resolution and cycle protection. Plotting does not reparse source text for every sample.

Evaluation errors are returned to the plotter as invalid samples. They produce gaps rather than screen coordinates.

### CE Rendering Controller

The CE layer owns render progress and connects plotter segments to GraphX. It tracks whether the current graph is absent, coarse, refining, or refined. The application controller invalidates that state when an expression, viewport, graph rectangle, view, or mode changes.

The controller also maps full-Graph arrow and zoom input to viewport actions. Existing menu dialogs remain modal, so graph navigation cannot continue behind an open menu.

## Sparse Expression Memory

The current Milestone 1 layout reserves a complete 128-node syntax-tree arena and two 256-byte text buffers for every one of 16 lines, consuming roughly 33 KB even when the project is blank. Milestone 2A replaces that layout.

The project keeps 16 small line descriptors so slot order remains stable. A blank descriptor owns no expression payload. A populated line owns one right-sized payload containing its committed source and only the syntax-tree nodes it actually uses. A single editor buffer is shared by the line currently being edited; inactive lines do not retain duplicate draft buffers.

Commit is transactional:

1. Parse and validate the draft in the existing shared scratch arena.
2. Allocate a replacement payload sized for the source and used nodes.
3. Rebuild affected dependencies against the replacement.
4. Swap the replacement into the line only after every step succeeds.
5. Release the prior payload after the successful swap.

Canceling an edit discards only the shared draft. Clearing a line releases its payload and updates dependents. If allocation fails, the previous committed line and dependency graph remain unchanged and the UI displays an insufficient-memory message.

Blank lines cannot consume literally zero bytes because their slot and state must exist, but they use only their small fixed descriptors. They do not reserve source arrays, syntax-tree arenas, sample grids, or drawing buffers.

The renderer uses one pair of sample rows shared across expressions. With five typical equations, project data plus renderer working memory should remain well below 20 KB, excluding program code and GraphX-owned display memory. Exact totals are verified on the physical calculator.

## Storage Compatibility

The saved project format advances from version 1 to version 2. Version 2 adds the 2D viewport fields while continuing to serialize expressions by value; in-memory pointers and generated graph data are never stored.

Loading follows these rules:

- Version 1 remains accepted and receives the default viewport.
- Version 2 validates finite viewport values and a positive scale before allocating expressions.
- Unknown versions are rejected without modifying the active project.
- A failed or partial allocation releases all staging data and leaves the active project intact.
- Saving retains the existing temporary-write, validation, replacement, and archive behavior.

The viewport is included on explicit save even when no expression changed. Merely panning or zooming does not force a save or trigger a quit warning.

## Error Handling

- One invalid or unfinished line does not stop other equations from drawing.
- A 3D or unsupported line is skipped rather than treated as a fatal error.
- Undefined names and numerical domain errors create gaps in only the affected graph.
- Non-finite samples never reach screen-coordinate conversion.
- Discontinuity validation rejects false contour bridges.
- Allocation failure preserves the last committed line and active project.
- Interrupted refinement is discarded and restarted from the newest state.
- Menu and quit controls remain responsive during or after graph rendering.

## Verification

### Host Tests

The calculator-independent tests cover:

- coordinate-to-screen and screen-to-coordinate mapping,
- panning and zooming,
- adaptive grid spacing and axis visibility,
- horizontal, diagonal, and vertical lines,
- a circle and ellipse,
- reversed equalities such as `25=x^2+y^2`,
- multiple simultaneous equalities,
- ambiguous marching-squares cells,
- no false bridge across `y=1/x`,
- undefined and non-finite sample gaps,
- clipping to both full Graph and Compact Split rectangles,
- skipped invalid, inequality, and 3D expressions,
- version 1 loading with a default viewport,
- version 2 viewport round trips and validation,
- sparse allocation on add, edit, clear, load, and project replacement,
- injected allocation failure preserving the prior expression and project, and
- repeated edit and clear cycles without increasing live allocations.

CE input and UI tests cover Graph-only navigation, Compact Split selection, safe `CLEAR` behavior in Graph view, the five top-key shortcuts, modal menu behavior, and the existing quit sequence.

### Physical Calculator Checks

Private builds are checked on the user's calculator for:

- visual correctness of lines, vertical lines, circles, ellipses, and `y=1/x`,
- coarse movement followed by visible refinement,
- responsive pan, zoom, menu, and quit controls,
- one, five, and 16 plotted equations,
- RAM usage for 0, 1, 5, and 16 populated slots,
- RAM recovery after clearing all expressions,
- repeated add, edit, and clear cycles,
- save and reload of viewport and expressions, and
- loading an existing version 1 project.

No speed or RAM claim is final until measured on the physical calculator.

## Success Criteria

Milestone 2A is complete when:

- Explicit, vertical, reversed, and implicit equalities render from the same expression list.
- Circles and ellipses do not require `y=` conversion.
- Five typical equations remain usable and all 16 populated slots remain supported.
- Panning and zooming stay responsive and refinement cancels cleanly.
- `y=1/x` does not draw a false segment through its asymptote.
- Blank slots do not reserve expression payloads or graph buffers.
- Allocation failure cannot destroy the previous expression or active project.
- Version 1 projects still load and version 2 projects preserve the viewport.
- Host tests and the physical-calculator checklist pass.
- Only private development artifacts exist; no shareable release has been created.
