# Milestone 1 Math Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a private, calculator-buildable Milestone 1 with the three approved views, a general relation parser and scalar evaluator, dependency-aware expression projects, and recoverable AppVar storage.

**Architecture:** Keep the editor, parser, evaluator, dependency graph, project codec, and view state as portable C11 modules compiled by both a host test runner and CEdev. Keep GraphX, keypadc, and fileioc behind CE-only adapters so computer tests exercise the same math and persistence logic used on the calculator.

**Tech Stack:** C11; CEdev v15.0; CE libraries `graphx`, `keypadc`, and `fileioc`; a PowerShell host-test wrapper using a C11 compiler supplied through `HOST_CC`; the standard CEdev makefile flow.

**Spec:** `docs/superpowers/specs/2026-10-16-ti84-desmos-style-graphing-calculator-design.md`

## Global Constraints

- Target the user's TI-84 Plus CE Python running arTIfiCE Shell 2.1.
- Pin calculator builds to CEdev v15.0; do not use a nightly toolchain.
- Use C11 for production code; do not add assembly until profiling proves a specific need.
- Keep all CE-only headers and calls under `src/platform/ce/`; portable tests must not include CE headers.
- Keep `Project`, codec buffers, and parser scratch arenas in static storage on the calculator; do not place multi-kilobyte objects on the eZ80 stack or allocate them from the heap.
- Use original code and branding; do not copy Desmos assets or source.
- Create private debug `.8xp` files only for testing. Do not publish, upload, archive as a release, or create a public/shareable distribution.
- Request user approval before downloading or installing CEdev or a host compiler.
- Use `DGCALC` as the private program name, `DGCTMP` as the temporary AppVar, and `DGCBAK` as the recovery AppVar; all names fit the calculator's eight-character variable limit.
- A failed edit or save must preserve the previous valid expression or project.
- Compile host tests with warnings treated as errors.

## Review Focus

- A 256th source byte must be rejected without changing the 255-byte expression already stored; pinned in Task 2.
- Excessively nested input or more than 128 AST nodes must return a deterministic parse-limit error without writing past the arena; pinned in Task 3.
- Undefined names and circular definitions must mark only affected lines while unrelated definitions remain evaluable; pinned in Task 5.
- Truncated, oversized, unsupported-version, or CRC-corrupt project bytes must be rejected without partially replacing the active project; pinned in Task 6.
- Failure at every write, read-back, rename, validation, or archive boundary must leave the prior final AppVar recoverable and remove or retain only documented temporary state; pinned in Task 7.

---

## Planned File Structure

```text
.gitignore                         Generated CE and host artifacts
makefile                           Standard CEdev v15 calculator build
README.md                          Private build, transfer, and key instructions
src/main.c                         CE entry point only
src/app/app.c                      Application/view state transitions
src/app/app.h                      Shared view, mode, and selection state
src/editor/editor.c                Bounded expression source editing
src/editor/editor.h                EditorBuffer API and capacity
src/math/ast.c                     Fixed-capacity immutable AST arena
src/math/ast.h                     AST node and operator types
src/math/lexer.c                   Source tokenization
src/math/lexer.h                   Lexer token and error API
src/math/parser.c                  Precedence parser and relation parsing
src/math/parser.h                  ParseResult and parser_parse
src/math/validate.c                Expression-role and variable validation
src/math/validate.h                ValidationResult and ExprRole
src/math/eval.c                    Scalar and relation evaluation
src/math/eval.h                    EvalContext and EvalResult
src/math/deps.c                    Dependency extraction and cycle detection
src/math/deps.h                    DependencyGraph API
src/project/project.c              Expression lines, commits, invalidation
src/project/project.h              Project and ExpressionLine API
src/storage/project_codec.c        Versioned binary format and CRC32
src/storage/project_codec.h        Encode/decode API and format constants
src/storage/atomic_store.c         Backend-neutral replace-and-rollback flow
src/storage/atomic_store.h         StorageBackend and StoreResult
src/platform/ce/ce_input.c         keypadc to AppAction/editor input mapping
src/platform/ce/ce_input.h         CE input polling API
src/platform/ce/ce_ui.c            GraphX rendering for the three views
src/platform/ce/ce_ui.h            CE UI lifecycle and drawing API
src/platform/ce/appvar_backend.c   fileioc StorageBackend implementation
src/platform/ce/appvar_backend.h   CE storage factory
tests/test.h                       Minimal assertion and test-registration macros
tests/test_main.c                  Host test runner
tests/app_state_test.c             View-state tests
tests/editor_test.c                Editor boundary tests
tests/parser_test.c                Lexer/parser/arena tests
tests/eval_test.c                  Validation and evaluation tests
tests/project_test.c               Dependencies and commit tests
tests/project_codec_test.c         Format and corruption tests
tests/atomic_store_test.c          Failure-injected save tests
tools/test-host.ps1                Compile and run portable C tests
tools/build-ce.ps1                 Validate CEdev v15 and invoke make
```

## Task 1: Build Harness and Three-View Application State

**Files:**

- Create: `.gitignore`
- Create: `makefile`
- Create: `src/main.c`
- Create: `src/app/app.c`
- Create: `src/app/app.h`
- Create: `tests/test.h`
- Create: `tests/test_main.c`
- Create: `tests/app_state_test.c`
- Create: `tools/test-host.ps1`
- Create: `tools/build-ce.ps1`

**Interfaces:**

- Produces: `AppView { APP_VIEW_EXPRESSIONS, APP_VIEW_SPLIT, APP_VIEW_GRAPH }`
- Produces: `GraphMode { GRAPH_MODE_2D, GRAPH_MODE_3D }`
- Produces: `AppAction { APP_ACTION_NONE, APP_ACTION_SHOW_EXPRESSIONS, APP_ACTION_SHOW_SPLIT, APP_ACTION_SHOW_GRAPH, APP_ACTION_QUIT }`
- Produces: `AppState { AppView view; GraphMode mode; uint8_t selected_expression; }`
- Produces: `void app_state_init(AppState *state)`
- Produces: `bool app_handle_action(AppState *state, AppAction action)`; returns `false` only for quit

- [ ] **Step 1: Establish approved toolchain inputs**

Confirm CEdev v15.0 is available outside a path containing spaces and set `CEDEV_HOME`. Confirm a host C11 compiler is available and set `HOST_CC` to its executable. If either is missing, request permission before downloading it. Verify:

```powershell
& "$env:CEDEV_HOME\bin\ez80-clang.exe" --version
& $env:HOST_CC --version
```

Expected: CEdev reports the v15 toolchain based on Clang 19, and the host compiler exits successfully.

- [ ] **Step 2: Write the host test harness and failing view-state test**

`tests/app_state_test.c` must contain `test_app_state_defaults_to_expressions_2d()` and `test_view_actions_preserve_mode()` with these assertions:

```c
AppState state;
app_state_init(&state);
ASSERT_EQ_INT(APP_VIEW_EXPRESSIONS, state.view);
ASSERT_EQ_INT(GRAPH_MODE_2D, state.mode);
ASSERT_EQ_INT(0, state.selected_expression);
state.mode = GRAPH_MODE_3D;
ASSERT_TRUE(app_handle_action(&state, APP_ACTION_SHOW_SPLIT));
ASSERT_EQ_INT(APP_VIEW_SPLIT, state.view);
ASSERT_EQ_INT(GRAPH_MODE_3D, state.mode);
```

`tools/test-host.ps1` must compile every `.c` file outside `src/platform/ce/` and `src/main.c`, plus every test file, using `-std=c11 -Wall -Wextra -Werror`, then run the resulting executable.

- [ ] **Step 3: Run the host test to verify it fails**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails because `src/app/app.h` or its functions do not exist.

- [ ] **Step 4: Implement the minimal application state**

Create the exact types and signatures above. Initialize `selected_expression` to zero. `APP_ACTION_SHOW_EXPRESSIONS`, `APP_ACTION_SHOW_SPLIT`, and `APP_ACTION_SHOW_GRAPH` change only `state.view`; `APP_ACTION_QUIT` returns `false`; all other actions return `true` without mutation.

- [ ] **Step 5: Run the host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: all application-state tests pass with zero compiler warnings.

- [ ] **Step 6: Add the private CE build skeleton**

Use the CEdev v15 standard project makefile with program name `DGCALC` and libraries `graphx keypadc fileioc`. `src/main.c` initializes `AppState` and exits immediately; UI and input arrive in Task 8. `tools/build-ce.ps1` must validate `CEDEV_HOME`, prepend its `bin` directory for the child process only, invoke `make`, and leave the generated debug program at `bin/DGCALC.8xp`.

- [ ] **Step 7: Build the calculator skeleton**

Run: `powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1`

Expected: exit code 0 and `bin/DGCALC.8xp` exists locally.

- [ ] **Step 8: Commit**

```powershell
git add .gitignore makefile src/main.c src/app tests tools
git commit -m "build: bootstrap calculator and host tests"
```

## Task 2: Bounded General Expression Editor

**Files:**

- Create: `src/editor/editor.c`
- Create: `src/editor/editor.h`
- Create: `tests/editor_test.c`

**Interfaces:**

- Produces: `#define EDITOR_CAPACITY 256`
- Produces: `EditorBuffer { char text[EDITOR_CAPACITY]; uint16_t length; uint16_t cursor; }`
- Produces: `void editor_init(EditorBuffer *editor)`
- Produces: `bool editor_insert(EditorBuffer *editor, char value)`
- Produces: `bool editor_backspace(EditorBuffer *editor)`
- Produces: `bool editor_delete(EditorBuffer *editor)`
- Produces: `bool editor_move(EditorBuffer *editor, int16_t delta)`
- Produces: `bool editor_set_text(EditorBuffer *editor, const char *text)`

- [ ] **Step 1: Write failing cursor and capacity tests**

Add `test_editor_inserts_and_deletes_at_cursor()` and `test_editor_rejects_256th_byte_without_mutation()`:

```c
EditorBuffer editor;
editor_init(&editor);
ASSERT_TRUE(editor_set_text(&editor, "x+1"));
ASSERT_TRUE(editor_move(&editor, -1));
ASSERT_TRUE(editor_insert(&editor, '2'));
ASSERT_STR_EQ("x+21", editor.text);

char full[EDITOR_CAPACITY];
memset(full, 'x', EDITOR_CAPACITY - 1);
full[EDITOR_CAPACITY - 1] = '\0';
ASSERT_TRUE(editor_set_text(&editor, full));
ASSERT_FALSE(editor_insert(&editor, '+'));
ASSERT_EQ_INT(EDITOR_CAPACITY - 1, editor.length);
ASSERT_STR_EQ(full, editor.text);
```

- [ ] **Step 2: Run the editor tests to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on the missing editor API.

- [ ] **Step 3: Implement the bounded editor**

Use `memmove` for insertion and deletion. Maintain a null terminator after every successful mutation. Reject invalid cursor movement and overflow without changing any field.

- [ ] **Step 4: Run all host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: editor and application-state tests pass.

- [ ] **Step 5: Commit**

```powershell
git add src/editor tests/editor_test.c
git commit -m "feat: add bounded expression editor"
```

## Task 3: Fixed-Arena Lexer and Relation Parser

**Files:**

- Create: `src/math/ast.c`
- Create: `src/math/ast.h`
- Create: `src/math/lexer.c`
- Create: `src/math/lexer.h`
- Create: `src/math/parser.c`
- Create: `src/math/parser.h`
- Create: `tests/parser_test.c`

**Interfaces:**

- Produces: `#define AST_MAX_NODES 128`
- Produces: `AstKind { AST_NUMBER, AST_SYMBOL, AST_UNARY, AST_BINARY, AST_CALL, AST_RELATION }`
- Produces: `AstOp { AST_OP_POS, AST_OP_NEG, AST_OP_ADD, AST_OP_SUB, AST_OP_MUL, AST_OP_DIV, AST_OP_POW, AST_OP_EQ, AST_OP_LT, AST_OP_LE, AST_OP_GT, AST_OP_GE }`
- Produces: `typedef uint16_t AstId` and `#define AST_NONE UINT16_MAX`
- Produces: `AstArena { AstNode nodes[AST_MAX_NODES]; uint16_t count; }`
- Produces: `ParseErrorCode { PARSE_OK, PARSE_ERR_TOKEN, PARSE_ERR_EXPECTED, PARSE_ERR_TRAILING, PARSE_ERR_NODE_LIMIT, PARSE_ERR_DEPTH_LIMIT }`
- Produces: `ParseResult { bool ok; AstId root; ParseErrorCode code; uint16_t offset; }`
- Produces: `ParseResult parser_parse(const char *source, AstArena *arena)`

- [ ] **Step 1: Write failing grammar, relation, and limit tests**

Add tests asserting:

```c
ASSERT_FLOAT_EQ(14.0f, test_parse_and_eval_literal("2+3*4"));
ASSERT_FLOAT_EQ(20.0f, test_parse_and_eval_literal("2(3+7)"));
ASSERT_EQ_INT(AST_RELATION, root_kind("25=x^2+y^2"));
ASSERT_EQ_INT(AST_OP_EQ, root_op("x^2+y^2=25"));
ASSERT_EQ_INT(PARSE_ERR_DEPTH_LIMIT, parse_nested_parentheses(65).code);
ASSERT_EQ_INT(PARSE_ERR_NODE_LIMIT, parse_repeated_additions(AST_MAX_NODES + 1).code);
```

The test-only literal evaluator may walk number and arithmetic nodes; production evaluation belongs to Task 4.

- [ ] **Step 2: Run parser tests to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on the missing AST/parser API.

- [ ] **Step 3: Implement the fixed arena and lexer**

Tokenize decimal numbers, single-letter user symbols, recognized multi-letter built-in identifiers, parentheses, comma, `+ - * / ^`, and `= < <= > >=`. Record the source offset for every token and error. Do not allocate from the heap.

- [ ] **Step 4: Implement the precedence parser**

Use right-associative exponentiation, then unary signs, implicit/explicit multiplication and division, addition/subtraction, and finally one top-level relation. Limit recursive parser depth to 64 and return the exact limit errors above before arena overflow.

- [ ] **Step 5: Run all host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: parser, editor, and application-state tests pass with zero warnings.

- [ ] **Step 6: Commit**

```powershell
git add src/math tests/parser_test.c
git commit -m "feat: parse general mathematical relations"
```

## Task 4: Validation and Scalar Evaluation

**Files:**

- Create: `src/math/validate.c`
- Create: `src/math/validate.h`
- Create: `src/math/eval.c`
- Create: `src/math/eval.h`
- Create: `tests/eval_test.c`

**Interfaces:**

- Consumes: `AstArena`, `AstId`, and AST operators from Task 3
- Produces: `ExprRole { EXPR_ROLE_INVALID, EXPR_ROLE_SCALAR, EXPR_ROLE_DEFINITION, EXPR_ROLE_FUNCTION_DEFINITION, EXPR_ROLE_RELATION_2D, EXPR_ROLE_RELATION_3D }`
- Produces: `ValidationErrorCode { VALIDATION_OK, VALIDATION_ERR_SHAPE, VALIDATION_ERR_VARIABLES, VALIDATION_ERR_DEFINITION }`
- Produces: `ValidationResult { bool ok; ExprRole role; ValidationErrorCode error; AstId error_node; }`
- Produces: `ValidationResult expression_validate(const AstArena *arena, AstId root)`
- Produces: `EvalError { EVAL_OK, EVAL_ERR_UNDEFINED, EVAL_ERR_CYCLE, EVAL_ERR_DOMAIN, EVAL_ERR_DIV_ZERO, EVAL_ERR_NONFINITE }`
- Produces: `typedef EvalError (*ResolveSymbolFn)(void *user, const char *name, float *value)`
- Produces: `typedef EvalError (*ResolveCallFn)(void *user, const char *name, float argument, float *value)`
- Produces: `EvalContext { ResolveSymbolFn resolve_symbol; ResolveCallFn resolve_call; void *user; }`
- Produces: `EvalResult { float value; EvalError error; }`
- Produces: `EvalResult expression_eval(const AstArena *arena, AstId root, const EvalContext *context)`
- Produces: `uint8_t expression_variable_mask(const AstArena *arena, AstId root)` using bit 0=`x`, bit 1=`y`, bit 2=`z`

- [ ] **Step 1: Write failing validation and evaluation tests**

Cover arithmetic, built-ins, domain failure, and dimensional roles:

```c
ASSERT_FLOAT_EQ(1.0f, eval_source("sin(pi/2)").value);
ASSERT_EQ_INT(EVAL_ERR_DIV_ZERO, eval_source("1/0").error);
ASSERT_EQ_INT(EVAL_ERR_DOMAIN, eval_source("sqrt(-1)").error);
ASSERT_EQ_INT(EXPR_ROLE_RELATION_2D, validate_source("y=x^2").role);
ASSERT_EQ_INT(EXPR_ROLE_RELATION_2D, validate_source("x=2").role);
ASSERT_EQ_INT(EXPR_ROLE_RELATION_2D, validate_source("25=x^2+y^2").role);
ASSERT_EQ_INT(EXPR_ROLE_RELATION_3D, validate_source("x^2+y^2+z^2=16").role);
ASSERT_EQ_INT(0x03, variable_mask_source("x^2+y^2=25"));
ASSERT_EQ_INT(VALIDATION_ERR_VARIABLES, validate_source("1=1").error);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on missing validation/evaluation symbols.

- [ ] **Step 3: Implement validation**

Recognize a lone non-coordinate symbol equality such as `a=2` as a scalar definition and a call-shaped left side such as `f(x)` as a single-argument function definition. The coordinate names `x`, `y`, and `z` always remain graph variables, so `x=2` and `y=x^2` are 2D relations while a relation containing `z` is 3D. Reject definitions that shadow constants or built-ins with `VALIDATION_ERR_DEFINITION`. Classify all other relations by their `x/y/z` variable mask. Reject a relation with no free graph variables using `VALIDATION_ERR_VARIABLES` rather than treating it as a plot. Every failing result must set `ok=false`, `role=EXPR_ROLE_INVALID`, and the closest available `error_node`.

- [ ] **Step 4: Implement scalar evaluation**

Support constants `pi` and `e`; built-ins `sin`, `cos`, `tan`, `sqrt`, `abs`, `exp`, `ln`, `log`, `floor`, and `ceil`; arithmetic; and relation residual evaluation as `left-right`. Use `resolve_symbol` for non-coordinate values supplied by the caller and `resolve_call` for non-built-in calls. Return explicit errors for undefined names, cycles, invalid domains, division by zero, and non-finite output.

- [ ] **Step 5: Run all host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: all tests pass with zero warnings.

- [ ] **Step 6: Commit**

```powershell
git add src/math tests/eval_test.c
git commit -m "feat: validate and evaluate expressions"
```

## Task 5: Dependency-Aware Expression Projects

**Files:**

- Create: `src/math/deps.c`
- Create: `src/math/deps.h`
- Create: `src/project/project.c`
- Create: `src/project/project.h`
- Create: `tests/project_test.c`

**Interfaces:**

- Consumes: editor, parser, validation, and evaluation APIs from Tasks 2–4
- Produces: `#define PROJECT_MAX_EXPRESSIONS 32`
- Produces: `ExpressionStatus { EXPRESSION_EMPTY, EXPRESSION_VALID, EXPRESSION_INVALID, EXPRESSION_UNDEFINED, EXPRESSION_CYCLE }`
- Produces: `ExpressionLine` with a committed source buffer, editable draft buffer, committed AST arena/root, status, and line-local parse/validation error fields
- Produces: `Project` containing 32 `ExpressionLine` entries, an expression count, and a dirty flag; view, mode, and selection remain solely in `AppState`
- Produces: `CommitResult { bool ok; ParseErrorCode parse_error; ValidationErrorCode validation_error; uint16_t error_offset; }`
- Produces: `void project_init(Project *project)`
- Produces: `bool project_set_draft(Project *project, uint8_t index, const char *source)`
- Produces: `CommitResult project_commit_line(Project *project, uint8_t index)`
- Produces: `EvalResult project_evaluate_line(Project *project, uint8_t index)`
- Produces: `void project_mark_dependents_dirty(Project *project, uint8_t index)`

- [ ] **Step 1: Write failing commit, dependency, and isolation tests**

Add tests for definitions and error isolation:

```c
project_set_and_commit(&project, 0, "a=2");
project_set_and_commit(&project, 1, "b=a+3");
ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);

project_set_draft(&project, 0, "a=");
ASSERT_FALSE(project_commit_line(&project, 0).ok);
ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);

project_set_and_commit(&project, 2, "c=d+1");
ASSERT_EQ_INT(EXPRESSION_UNDEFINED, project.lines[2].status);
ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);

project_set_and_commit(&project, 3, "d=c+1");
ASSERT_EQ_INT(EXPRESSION_CYCLE, project.lines[2].status);
ASSERT_EQ_INT(EXPRESSION_CYCLE, project.lines[3].status);
```

Also test `f(x)=x^2+a` followed by an expression that evaluates `f(3)`.

- [ ] **Step 2: Run project tests to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on the missing project/dependency API.

- [ ] **Step 3: Implement dependency extraction and cycle detection**

Represent dependencies and reverse dependencies as one 32-bit bitset per expression. Exclude built-ins, graph variables, and a function's local parameter. Use a three-color depth-first traversal for cycles and mark every member of a detected cycle.

- [ ] **Step 4: Implement transactional line commits**

Parse and validate through one reusable static scratch arena. On failure, keep the prior committed source, AST, and dependent values, while preserving the invalid draft and its error for display. On success, copy the scratch tree into the line's committed arena, replace the committed source, rebuild dependencies, invalidate only reverse dependents, and set `project.dirty=true`.

- [ ] **Step 5: Implement project symbol/function resolution**

Implement `ResolveSymbolFn` and `ResolveCallFn` adapters over the dependency graph. Resolve scalar definitions and single-argument user functions through those adapters, using function-local parameter bindings before global definitions. Return `EVAL_ERR_UNDEFINED` or `EVAL_ERR_CYCLE` without evaluating affected lines.

- [ ] **Step 6: Run all host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: all tests pass, including unrelated-line evaluation after undefined names and cycles.

- [ ] **Step 7: Commit**

```powershell
git add src/math/deps.* src/project tests/project_test.c
git commit -m "feat: manage expression dependencies"
```

## Task 6: Versioned and Validated Project Codec

**Files:**

- Create: `src/storage/project_codec.c`
- Create: `src/storage/project_codec.h`
- Create: `tests/project_codec_test.c`

**Interfaces:**

- Consumes: `AppState`, `Project`, and editor source limits from Tasks 1 and 5
- Produces: `#define PROJECT_FORMAT_VERSION 1`
- Produces: `#define PROJECT_MAGIC "DGCE"`
- Produces: `#define PROJECT_MAX_BYTES (13 + 4 + PROJECT_MAX_EXPRESSIONS * (4 + 2 * (EDITOR_CAPACITY - 1)))`
- Produces: `CodecError { CODEC_OK, CODEC_ERR_CAPACITY, CODEC_ERR_MAGIC, CODEC_ERR_VERSION, CODEC_ERR_LENGTH, CODEC_ERR_CRC, CODEC_ERR_CONTENT }`
- Produces: `size_t project_encode(const AppState *state, const Project *project, uint8_t *output, size_t capacity, CodecError *error)`
- Produces: `bool project_decode(AppState *state_destination, Project *project_destination, const uint8_t *input, size_t length, CodecError *error)`
- Produces: `uint32_t project_crc32(const uint8_t *data, size_t length)`

The v1 byte format is little-endian and contains: four-byte magic, one-byte version, four-byte payload length, four-byte CRC32, one-byte graph mode, one-byte view, one-byte selected line, one-byte expression count, then for each line a two-byte committed-source length and bytes followed by a two-byte draft-source length and bytes. A zero committed length means the line has no last-valid expression. Status, AST nodes, dependency caches, and the dirty flag are rebuilt rather than serialized.

- [ ] **Step 1: Write failing round-trip and corruption tests**

Test an empty project and a project with valid plus invalid drafts. Then assert:

```c
ASSERT_FALSE(project_decode(&state_destination, &project_destination,
                            truncated, truncated_length, &error));
ASSERT_EQ_INT(CODEC_ERR_LENGTH, error);
ASSERT_FALSE(project_decode(&state_destination, &project_destination,
                            bad_crc, full_length, &error));
ASSERT_EQ_INT(CODEC_ERR_CRC, error);
ASSERT_FALSE(project_decode(&state_destination, &project_destination,
                            unsupported_version, full_length, &error));
ASSERT_EQ_INT(CODEC_ERR_VERSION, error);
ASSERT_APP_STATE_EQ(state_before, state_destination);
ASSERT_PROJECT_EQ(project_before, project_destination);
```

Add a deterministic loop that feeds 1,000 bounded pseudo-random byte arrays to `project_decode` and asserts both destinations change only when the decoder returns `true`.

- [ ] **Step 2: Run codec tests to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on the missing codec API.

- [ ] **Step 3: Implement CRC32 and explicit byte encoding**

Use polynomial `0xEDB88320`. Write and read integer fields byte-by-byte rather than serializing C structs, so host and eZ80 padding cannot affect the format.

- [ ] **Step 4: Implement transactional decoding**

Decode in two passes without copying a whole `Project`. The first pass validates magic, version, payload length, CRC, mode, view, `selected_expression < PROJECT_MAX_EXPRESSIONS`, expression count, both source lengths per line, complete payload consumption, and every nonempty committed source using the reusable parser scratch arena; it must not mutate either destination. Only after that pass succeeds, initialize the project destination, replay the already-validated committed sources through `project_commit_line`, restore each draft separately, then assign the app state. Invalid drafts are preserved rather than treated as corrupt bytes. Because the second pass uses fixed-capacity operations already proven by the first, it has no recoverable failure path. A successful load sets `project_destination->dirty=false`.

- [ ] **Step 5: Run all host tests**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: all tests pass, including the 1,000 malformed inputs.

- [ ] **Step 6: Commit**

```powershell
git add src/storage/project_codec.* tests/project_codec_test.c
git commit -m "feat: encode versioned calculator projects"
```

## Task 7: Recoverable AppVar Storage

**Files:**

- Create: `src/storage/atomic_store.c`
- Create: `src/storage/atomic_store.h`
- Create: `src/platform/ce/appvar_backend.c`
- Create: `src/platform/ce/appvar_backend.h`
- Create: `tests/atomic_store_test.c`

**Interfaces:**

- Consumes: project codec from Task 6
- Produces: `StorageBackend` containing `void *user` and callbacks `bool exists(void *user, const char *name)`, `bool write_all(void *user, const char *name, const uint8_t *data, size_t length)`, `bool read_all(void *user, const char *name, uint8_t *data, size_t capacity, size_t *length)`, `bool delete(void *user, const char *name)`, `bool rename(void *user, const char *old_name, const char *new_name)`, and `bool set_archived(void *user, const char *name, bool archived)`
- Produces: `StoreError { STORE_OK, STORE_ERR_ENCODE, STORE_ERR_WRITE, STORE_ERR_VALIDATE, STORE_ERR_RENAME, STORE_ERR_ARCHIVE, STORE_ERR_READ }`
- Produces: `StoreResult { bool ok; StoreError error; }`
- Produces: `StoreResult project_store_atomic(StorageBackend *backend, const char *final_name, const AppState *state, const Project *project)`
- Produces: `StoreResult project_store_load(StorageBackend *backend, const char *name, AppState *state_destination, Project *project_destination)`
- Produces: `StorageBackend ce_appvar_backend(void)`

- [ ] **Step 1: Write the failure-injected atomic-save tests**

Create an in-memory fake backend with an operation counter and fail-on-operation setting. Its `write_all` test double must also support partial-write and reported-close-failure modes so read-back validation covers incomplete persistence. For each possible failure boundary, start with a valid `DGCPROJ`, attempt a save, and assert:

```c
ASSERT_FALSE(result.ok);
ASSERT_BYTES_EQ(old_project_bytes, fake_storage_bytes(&backend, "DGCPROJ"));
ASSERT_FALSE(fake_storage_exists(&backend, "DGCTMP"));
```

On success assert the final bytes decode to the new state and project, `DGCTMP` and `DGCBAK` are absent, and `fake_storage_is_archived(&backend, "DGCPROJ")` is true. Add a load test proving corrupt bytes leave both destinations unchanged.

- [ ] **Step 2: Run atomic-store tests to verify they fail**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails on the missing storage API.

- [ ] **Step 3: Implement the backend-neutral atomic sequence**

Use one static `PROJECT_MAX_BYTES` work buffer. Encode and write `DGCTMP`; read it back into that buffer and decode it; remove a stale `DGCBAK`; rename an existing final to `DGCBAK`; rename `DGCTMP` to the final name; archive the new final; then delete `DGCBAK`. At any failure after backing up the old final, delete the incomplete new final if present and rename `DGCBAK` back. Always attempt temporary cleanup before returning.

- [ ] **Step 4: Implement the CE fileioc backend**

Use `ti_Open`, `ti_Write`, `ti_Read`, `ti_GetSize`, and `ti_Close` for complete-file operations. Close every handle before `ti_Rename` or `ti_Delete`. Use `ti_SetArchiveStatus` only after the final AppVar validates, and install garbage-collection callbacks that temporarily call `gfx_End` and then restore GraphX.

- [ ] **Step 5: Run host tests and calculator build**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Run: `powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1`

Expected: all host tests pass and `bin/DGCALC.8xp` builds successfully.

- [ ] **Step 6: Commit**

```powershell
git add src/storage/atomic_store.* src/platform/ce/appvar_backend.* tests/atomic_store_test.c
git commit -m "feat: save projects without losing prior data"
```

## Task 8: Calculator Input, Three Views, and Milestone Verification

**Files:**

- Create: `src/platform/ce/ce_input.c`
- Create: `src/platform/ce/ce_input.h`
- Create: `src/platform/ce/ce_ui.c`
- Create: `src/platform/ce/ce_ui.h`
- Modify: `src/app/app.c`
- Modify: `src/app/app.h`
- Modify: `src/main.c`
- Modify: `tests/app_state_test.c`
- Create: `README.md`

**Interfaces:**

- Consumes: every Milestone 1 module
- Produces: `APP_ACTION_MOVE_SELECTION_UP`, `APP_ACTION_MOVE_SELECTION_DOWN`, `APP_ACTION_BEGIN_EDIT`, `APP_ACTION_COMMIT_EDIT`, `APP_ACTION_CANCEL_EDIT`, `APP_ACTION_NEW_PROJECT`, `APP_ACTION_SAVE_PROJECT`, `APP_ACTION_LOAD_PROJECT`, and `APP_ACTION_TOGGLE_GRAPH_MODE` in the portable `AppAction` API
- Produces: `AppAction ce_input_poll(EditorBuffer *active_editor, bool editing)`
- Produces: `void ce_ui_begin(void)`, `void ce_ui_draw(const AppState *state, const Project *project)`, and `void ce_ui_end(void)`
- Produces: a private `bin/DGCALC.8xp` for physical-calculator verification

- [ ] **Step 1: Extend failing action tests for approved keys**

Add portable tests for the abstract actions the CE mapper emits:

```c
app_handle_action(&state, APP_ACTION_SHOW_EXPRESSIONS);
ASSERT_EQ_INT(APP_VIEW_EXPRESSIONS, state.view);
app_handle_action(&state, APP_ACTION_SHOW_SPLIT);
ASSERT_EQ_INT(APP_VIEW_SPLIT, state.view);
app_handle_action(&state, APP_ACTION_SHOW_GRAPH);
ASSERT_EQ_INT(APP_VIEW_GRAPH, state.view);
ASSERT_EQ_INT(selected_before, state.selected_expression);
app_handle_action(&state, APP_ACTION_TOGGLE_GRAPH_MODE);
ASSERT_EQ_INT(GRAPH_MODE_3D, state.mode);
ASSERT_EQ_INT(selected_before, state.selected_expression);
```

Also assert that invalid draft text survives every view change and that toggling graph mode does not alter any expression or selection.

- [ ] **Step 2: Run host tests to verify the new state assertions fail where behavior is missing**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Expected: compilation fails because `APP_ACTION_TOGGLE_GRAPH_MODE` is not defined yet.

- [ ] **Step 3: Add the graph-mode action**

Add the Task 8 actions to `AppAction`. Handle `APP_ACTION_TOGGLE_GRAPH_MODE` in `app_handle_action` by switching only between `GRAPH_MODE_2D` and `GRAPH_MODE_3D`; the application loop handles project/edit/storage actions because they require more than `AppState`. Run `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1` and confirm the new state tests pass.

- [ ] **Step 4: Implement CE keypad mapping**

Use keypadc continuous scanning and rising-edge detection. Map `Y=` to Expressions, `MODE` to Split, and `GRAPH` to Graph. In Expressions, map arrows to line/cursor movement, `ENTER` to edit or commit, `DEL` to backspace, number/operator/function keys to editor input, and `PRGM` to a small application menu containing New, Save, Load, 2D/3D Mode, and Quit. Return the corresponding portable action for every command that the application loop must handle. `CLEAR` cancels editing before it can quit the app.

- [ ] **Step 5: Implement GraphX rendering**

Use buffered GraphX drawing. Expressions shows sources and line-local error text; Split shows the compact expression list and a graph placeholder; Graph shows the full graph placeholder. Both placeholders state that plotting arrives in Milestone 2. Display the active 2D/3D mode and preserve project selection across view changes.

- [ ] **Step 6: Integrate the application loop**

`src/main.c` must keep `AppState` and `Project` in static storage, initialize the project and UI, load `DGCPROJ` when requested, dispatch actions, draw only after state changes, and call `ce_ui_end()` on every normal exit. Save through `project_store_atomic`; set `project.dirty=false` only after a successful save, and show storage errors without discarding the in-memory project. New and Quit require save/discard/cancel confirmation when the project is dirty.

- [ ] **Step 7: Run complete automated verification**

Run: `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1`

Run: `powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1`

Expected: zero host-test failures, zero compiler warnings, and a private `bin/DGCALC.8xp`.

- [ ] **Step 8: Perform physical-calculator checks**

Transfer the private debug program with TI Connect CE and launch it through arTIfiCE Shell 2.1. Verify:

- `Y=`, `MODE`, and `GRAPH` switch views without losing selection, edits, or mode.
- A circle relation such as `x^2+y^2=25` can be entered and is classified as a valid 2D relation even though Milestone 1 does not draw it.
- `x^2+y^2+z^2=16` is classified as a valid 3D relation.
- Invalid syntax shows a line-local error and leaves previous committed state intact.
- A saved project reloads after quitting and relaunching.
- Forced low-storage or save failure leaves the previous saved project readable.

Record the calculator OS version, CEdev version, program size, and observed issues in `README.md`. Do not create release notes or a distribution archive.

- [ ] **Step 9: Commit**

```powershell
git add src/app src/main.c src/platform/ce/ce_input.* src/platform/ce/ce_ui.* tests/app_state_test.c README.md
git commit -m "feat: complete calculator math foundation"
```

## Final Milestone 1 Verification

- [ ] Run `powershell -ExecutionPolicy Bypass -File tools/test-host.ps1` and confirm all tests pass.
- [ ] Run `powershell -ExecutionPolicy Bypass -File tools/build-ce.ps1` and confirm `bin/DGCALC.8xp` is built privately.
- [ ] Run `git status --short` and confirm only intentionally retained local artifacts are ignored.
- [ ] Review every Milestone 1 item in the approved spec and record any physical-hardware limitation before designing Milestone 2.

## Primary References

- CEdev v15.0 release: https://github.com/CE-Programming/toolchain/releases/tag/v15.0
- CEdev project structure and build flow: https://ce-programming.github.io/toolchain/static/getting-started.html
- GraphX: https://ce-programming.github.io/toolchain/libraries/graphx.html
- keypadc: https://ce-programming.github.io/toolchain/libraries/keypadc.html
- fileioc: https://ce-programming.github.io/toolchain/libraries/fileioc.html
