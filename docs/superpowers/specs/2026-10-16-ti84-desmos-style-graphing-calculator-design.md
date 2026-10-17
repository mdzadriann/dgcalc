# TI-84 Desmos-Style Graphing Calculator Design

**Date:** 2026-10-16  
**Status:** Approved

## Purpose

Build a native, offline graphing application for the TI-84 Plus CE Python that is launched through arTIfiCE Shell 2.1. The application should preserve the flexible expression model and familiar workflow of Desmos while adapting the interface and rendering quality to the calculator's 320×240 display and constrained eZ80 hardware.

The application is initially for personal use. Development remains private and local. Private debug builds may be transferred to the user's calculator for testing, but no public download, release archive, or distributable package may be created until the user explicitly approves the final version.

## Goals

- Accept arbitrary mathematical expressions and relations instead of fixed `Y=` slots.
- Plot both 2D and 3D mathematics from one shared expression engine.
- Provide Expressions, Split, and Graph views suitable for the calculator screen.
- Support progressive, testable delivery without sacrificing validation, saved-data safety, or keypad usability.
- Prefer reliable C and established CE libraries; use eZ80 assembly only for measured bottlenecks.
- Use original branding, icons, and source code rather than copying Desmos assets or implementation.

## Non-Goals

- Desmos accounts, cloud saving, collaboration, share links, or other internet-dependent services.
- A public or shareable release before explicit final approval.
- Pixel-for-pixel duplication of Desmos branding or interface assets.
- Desktop-resolution 3D output or unlimited symbolic computation on calculator hardware.
- A full assembly implementation.

## Target Platform

- TI-84 Plus CE Python
- arTIfiCE Shell 2.1
- Native eZ80 program built with the CE C/C++ Toolchain
- C as the default implementation language
- CE libraries for graphics, keypad input, and AppVar storage
- Assembly limited to small routines whose performance benefit is demonstrated by profiling

## User Experience

### View Modes

The same project and expression list remain active while the user switches among three views:

1. **Expressions:** The full screen displays editable expressions, sliders, errors, and style controls.
2. **Split:** A compact expression list appears beside the active graph.
3. **Graph:** The full screen displays the graph and navigation overlays.

The approved initial key mapping is:

- `Y=` opens Expressions.
- `MODE` opens Split.
- `GRAPH` opens Graph.

Within this application, `Y=` is only a physical shortcut to the general expression list. It does not restrict entries to functions of the form `y=f(x)`. Settings normally associated with `MODE` will be available from an application menu.

The three views apply to both 2D and 3D projects. Switching views must preserve selected expressions, viewport or camera state, and unsaved edits.

### Expression List

Each line represents an arbitrary mathematical object. It may contain a relation, definition, value, list, point, slider, restriction, or note. A line can be enabled, disabled, selected, edited, or styled without changing the other lines.

Examples include:

- `y=sin(x)`
- `x=y^2`
- `x^2+y^2=25`
- `9x^2+4y^2=36`
- `25=x^2+y^2`
- `x^2+y^2<=25`
- `z=sin(x)cos(y)`
- `x^2+y^2+z^2=16`

The editor must support calculator-key entry of variables, operators, parentheses, comparisons, common functions, and reusable definitions. Display formatting may be two-dimensional, but the parser receives a normalized token sequence rather than screen coordinates.

## Expression Model

### Parsing

The parser converts each line into an abstract syntax tree. It supports:

- Numeric literals and named constants
- Unary and binary arithmetic
- Comparisons and equalities
- Variables and user-defined functions
- Lists, points, and tables
- Boolean combinations and domain or range restrictions
- Piecewise expressions
- Polar and parametric forms
- 2D and 3D coordinates

An equality is normalized as `left-right=0`. Therefore, `25=x^2+y^2` and `x^2+y^2=25` produce the same implicit relation. The graph classifier uses the variables and operators in the parsed tree, not a fixed left-hand variable, to select a plotting method.

### Dependencies

The evaluator maintains a dependency graph among variables, functions, sliders, lists, and plotted expressions. Editing a value invalidates only its dependents. Circular definitions are rejected and identify every line in the cycle.

### Supported Graph Classes

The complete target feature set includes:

- Explicit 2D curves in either axis orientation
- Implicit 2D curves
- 2D inequalities and shaded regions
- Polar curves
- Parametric 2D curves
- Points, connected points, lists, and tables
- Sliders and animation
- Restrictions and piecewise expressions
- Explicit 3D surfaces in any axis orientation
- Implicit 3D surfaces
- Parametric 3D curves and surfaces
- 3D points, planes, spheres, cylinders, and other relations
- Numerical intersections, zeros, extrema, derivatives, and integrals
- Statistics and regressions
- Complex values, vectors, and matrices where calculator memory and performance permit useful behavior

## Architecture

### Modules

1. **Application shell**
   - Owns startup, shutdown, menus, project lifecycle, and fatal-error recovery.

2. **Math editor**
   - Converts keypad actions into tokens and presents formatted expressions.
   - Does not evaluate or render mathematics.

3. **Parser and validator**
   - Produces immutable expression trees and precise line-local errors.
   - Normalizes relations and restrictions.

4. **Dependency evaluator**
   - Resolves definitions and evaluates scalar, list, point, and function values.
   - Caches parsed trees and evaluated values.

5. **Graph classifier**
   - Selects direct sampling, contour extraction, region filling, or surface extraction from the validated expression.

6. **2D renderer**
   - Draws grids, axes, explicit and parametric curves, implicit contours, inequalities, points, labels, and trace indicators.

7. **3D renderer**
   - Maintains the camera, projects geometry, clips it to the viewport, and draws points, curves, meshes, and extracted implicit surfaces.

8. **View controller**
   - Presents Expressions, Split, and Graph without duplicating expression or viewport state.

9. **Project storage**
   - Saves and loads versioned AppVars using validation and recoverable replacement.

These modules communicate through typed C structures and explicit ownership. The parser and evaluator contain no calculator-display dependencies so they can be compiled and tested on the development computer.

## Data Flow

Each expression follows this pipeline:

`key input -> tokens -> parse -> validate -> dependency update -> classify -> evaluate/sample -> drawable data -> render`

Editing a line creates a new parsed tree only after validation succeeds. Until then, the last valid plot may remain visible with the line marked as invalid. Committing a valid edit replaces the previous tree, updates the dependency graph, and marks affected samples dirty.

Rendering reads immutable sampled data. It cannot mutate expressions or saved project state.

## Plotting Strategy

### 2D

- Direct adaptive sampling for explicit functions
- Parametric sampling for parametric and polar curves
- Marching squares for implicit relations
- Cell classification and boundary tracing for inequalities
- Screen-space clipping before drawing
- Refinement concentrated where curvature or discontinuities require it

### 3D

- Grid sampling for explicit surfaces
- Marching cubes or an equivalently bounded lookup-table method for implicit surfaces
- Parametric sampling for curves and surfaces
- Software projection, clipping, depth ordering, and optional coarse shading
- Coarse geometry during rotation or zooming
- Refined geometry after interaction stops

The initial implementation uses ordinary C arithmetic for correctness. Fixed-point arithmetic, lookup tables, and assembly are introduced only when profiling identifies a specific routine that prevents an accepted interaction from remaining responsive.

## Performance and Memory

- Parsed trees, evaluated definitions, and sampled geometry are cached.
- A dirty-dependency system limits recalculation to affected expressions.
- 2D and 3D sample density scale with available memory and current interaction.
- Interactive 3D navigation favors coarse geometry and immediate keypad response over detail.
- Refinement is interruptible; new input cancels outdated work.
- Allocation failures reduce sampling detail and produce a visible warning instead of terminating the program.
- An expression or project too large for available memory must fail safely without corrupting saved data.

Real-hardware profiling determines final sample ceilings. This calibration changes quality limits, not supported expression syntax or saved-project compatibility.

## Storage and Recovery

Projects are stored as versioned calculator AppVars containing:

- Format version
- Project mode and view state
- Expression token streams and style settings
- Variables, slider settings, tables, and notes
- 2D viewport or 3D camera state
- Payload length and integrity check

Saving writes a temporary AppVar, validates it, and then replaces the previous project. A failed save leaves the previous valid project intact. Loading rejects unsupported or corrupted data with an actionable message and never attempts to evaluate unvalidated bytes.

Generated sample buffers are not stored because they can be rebuilt from expressions.

## Error Handling

- Syntax errors belong to the edited line and do not disable unrelated plots.
- Undefined names identify the missing definition.
- Circular dependencies identify all participating lines.
- Numerical domain errors create gaps instead of invalid screen coordinates.
- Discontinuities stop adaptive sampling across the invalid interval.
- Rendering failure disables only the affected drawable and preserves the project.
- Recoverable memory pressure lowers detail and displays a warning.
- Fatal startup or storage errors return safely to the shell when possible.

## Testing

### Computer-Based Tests

Portable parser, validator, dependency, evaluator, and project-format modules are compiled for the development computer. Tests cover:

- Operator precedence and associativity
- Equivalent relation normalization
- Definitions and dependency invalidation
- Circular dependencies
- Restrictions and piecewise expressions
- Known scalar and list results
- Project serialization, version handling, truncation, and corruption

### Mathematical Golden Cases

Representative expected outputs include:

- Lines and discontinuous rational functions
- Circles and ellipses written in multiple equivalent forms
- Inequalities and compound restrictions
- Polar roses and parametric loops
- Intersections, roots, and extrema
- Planes, spheres, cylinders, and explicit surfaces
- Implicit 3D surfaces at multiple detail levels

Tests compare evaluated values and extracted geometry invariants rather than exact pixel colors wherever possible.

### Calculator Verification

Each milestone is checked on the user's physical TI-84 Plus CE Python for:

- Key mapping and expression entry
- Expressions, Split, and Graph transitions
- Pan, zoom, trace, camera rotation, and cancellation responsiveness
- Memory-pressure behavior
- Save, reload, and recovery behavior
- Representative 2D and 3D rendering time

No performance claim is final until it is observed on the real calculator.

## Delivery Sequence

### Milestone 1: Math Foundation

- Application shell and view controller
- General expression editor
- Parser, validator, evaluator, and dependencies
- Versioned project storage

### Milestone 2: Complete 2D Core

- Explicit and implicit equations
- Circles, ellipses, inequalities, and restrictions
- Polar and parametric curves
- Multiple expressions, styles, pan, zoom, and trace
- Sliders, lists, points, and tables

### Milestone 3: 3D Core

- Points, curves, planes, and explicit surfaces
- Implicit spheres, cylinders, and arbitrary relations
- Camera rotation, zooming, clipping, and adjustable detail
- Expressions, Split, and Graph views in 3D

### Milestone 4: Advanced Desmos-Style Features

- Piecewise expressions and numerical analysis tools
- Statistics and regressions
- Complex numbers, vectors, and matrices where useful
- Labels, folders, notes, animations, and accessibility settings
- Final performance, storage, and recovery polish

Each milestone must pass its applicable computer tests and physical-calculator checks before the next milestone begins.

## Planning Boundary

This document defines the product-wide architecture and compatibility rules. The next implementation plan covers **Milestone 1: Math Foundation only**. Milestones 2 through 4 each receive their own reviewed design supplement and implementation plan after the preceding milestone passes its tests and calculator checks. Later work must preserve the expression, storage, error-isolation, and distribution contracts defined here.

## Distribution Guardrail

Development source, tests, and private debug calculator files remain local. The project must not be published, uploaded, publicly packaged, or presented as a shareable release until the user explicitly approves the final version. Final approval authorizes preparing a distributable; it does not authorize posting it publicly without a separate request.

## Risks and Mitigations

- **Implicit 3D is slow:** use coarse interactive meshes, interruptible refinement, and calibrated detail ceilings.
- **Floating-point arithmetic is slow:** retain correct C arithmetic first, then optimize only measured bottlenecks.
- **Expression scope grows uncontrollably:** deliver the approved milestones sequentially and keep one shared parser rather than feature-specific parsers.
- **Saved data becomes incompatible:** use explicit versions, validated lengths, and migration at the storage boundary.
- **An invalid expression destabilizes other graphs:** isolate errors by expression and render immutable sampled data.
- **Calculator OS changes affect native execution:** target the user's confirmed arTIfiCE Shell 2.1 environment and document the tested OS/toolchain combination before release.

## Success Criteria

The project is ready for final approval when:

- The calculator accepts and plots the representative 2D and 3D relations in this specification.
- Expressions, Split, and Graph views preserve shared state and operate through the approved keypad workflow.
- Invalid input, memory pressure, and corrupted save data fail safely.
- All computer-based tests pass.
- Required calculator checks pass on the user's physical device.
- Remaining hardware-driven quality limits are documented and accepted by the user.
- No public or shareable release has been created before that approval.
