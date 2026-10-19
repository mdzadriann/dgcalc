# Calculator Controls and Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Apply the approved calculator controls, one-press expression clearing, `2nd`+`MODE` exit, and a frozen menu background.

**Architecture:** Put expression removal in the portable `Project` API so dependent-expression state can be tested. Keep physical key routing in the existing CE input module and fix modal compositing once in the shared CE dialog renderer.

**Tech Stack:** C11, CEdev v15, keypadc, GraphX, existing assert-based host tests

**Spec:** `docs/superpowers/specs/2026-10-18-calculator-controls-and-menu.md`

## Global Constraints

- No new dependency or input abstraction.
- No public or shareable package.
- Preserve Save/Discard/Cancel protection.
- Host-test portable state and CEdev-compile calculator-only behavior.

## Review Focus

- Clearing a definition must make expressions that use it undefined.
- Clearing an invalid draft must also remove its previously committed expression.
- An empty expression must stay empty rather than become a syntax error when committed.
- A standalone `MODE` press must not quit; only latched `2nd`, then `MODE`, may quit.
- Modal redraws must preserve the visible background rather than alternate buffers.

---

### Task 1: Portable expression clearing

**Files:**
- Modify: `src/project/project.h`
- Modify: `src/project/project.c`
- Modify: `tests/project_test.c`

**Interfaces:**
- Produces: `bool project_clear_line(Project *project, uint8_t index)`
- Produces: empty-draft commits that succeed and leave the line empty

- [ ] **Step 1: Write the failing tests**

Add tests that clear a committed definition, verify its draft and committed state are empty, verify a dependent becomes undefined, and verify committing the empty line succeeds.

- [ ] **Step 2: Run the host suite and verify RED**

Run: `tools/test-host.ps1`
Expected: compilation fails because `project_clear_line` is not declared.

- [ ] **Step 3: Implement the minimum project-state reset**

Reset the selected line, mark the project dirty only when content existed, and rebuild dependencies. Treat an empty draft as a successful clear in `project_commit_line`.

- [ ] **Step 4: Run the host suite and verify GREEN**

Run: `tools/test-host.ps1`
Expected: all checks pass with 0 failures.

- [ ] **Step 5: Commit**

Commit message: `feat: add one-press expression clearing`

### Task 2: CE controls and stable menus

**Files:**
- Modify: `src/app/app.h`
- Modify: `src/app/app.c`
- Modify: `src/main.c`
- Modify: `src/platform/ce/ce_input.c`
- Modify: `src/platform/ce/ce_ui.c`
- Modify: `README.md`

**Interfaces:**
- Consumes: `project_clear_line(Project *, uint8_t)` from Task 1
- Produces: `APP_ACTION_CLEAR_EXPRESSION`

- [ ] **Step 1: Add the clear action and approved physical-key routing**

Route the five top keys exactly as specified, keep `DEL` as single-character backspace, route `CLEAR` to the clear action, and recognize sequential `2nd` then `MODE` as quit in both the main input loop and modal menus.

- [ ] **Step 2: Keep modal backgrounds fixed**

Call `gfx_BlitScreen()` before the shared dialog draws into the back buffer.

- [ ] **Step 3: Update visible help and README controls**

Use a compact bottom key legend and document `CLEAR`, `DEL`, and `2nd`+`MODE`.

- [ ] **Step 4: Run full verification**

Run: `tools/test-host.ps1`
Expected: all checks pass with 0 failures.

Run: `tools/build-ce.ps1`
Expected: exit 0 and `bin/DGCALC.8xp` exists.

- [ ] **Step 5: Commit**

Commit message: `fix: align calculator controls and stabilize menus`
