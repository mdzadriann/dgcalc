# DGCALC — private calculator build

DGCALC is an original offline graphing-calculator project for the TI-84 Plus CE Python. It uses a general expression engine rather than factory-style `Y=` slots, so the math foundation accepts relations such as `25=x^2+y^2` and `x^2+y^2+z^2=16` in either orientation.

This repository is still a private development build. Do not publish, upload, or package `DGCALC.8xp`. The local file is only for the user's private calculator test. Milestone 2A adds 2D equality plotting to the expression editor, definitions, functions, project saving, and three approved views. Explicit (`y=x`), vertical (`x=2`), reversed (`25=x^2+y^2`), circle, ellipse, and general implicit equalities share the same renderer. 3D remains a labeled placeholder. Inequalities, filled regions, polar/parametric curves, trace/analysis, and other later Desmos classes remain deferred.

All 16 expression positions remain available, with 128 AST nodes allowed per expression. Blank slots own no expression payload; populated slots hold only their committed source and used nodes. One shared editor and one shared row-based render job serve every slot. Calculator speed and RAM usage still require physical measurement.

## Build

The calculator build is pinned to CEdev v15.0:

```powershell
$env:CEDEV_HOME = 'C:\CEdev'
.\tools\build-ce.ps1
```

The private output is `bin/DGCALC.8xp`. Transfer it with TI Connect CE, then launch `DGCALC` through arTIfiCE Shell 2.1.

Host tests use the verified workspace Zig compiler as a self-contained C11 driver:

```powershell
$env:HOST_CC = 'C:\Users\Adrian\Documents\CHAT GPT PROJECTS\.toolchains\zig-x86_64-windows-0.17.0\zig.exe'
.\tools\test-host.ps1
```

## Calculator controls

- `Y=` opens Expressions, `WINDOW` opens the compact Split view, `ZOOM` toggles 2D/3D, `TRACE` opens or closes the project menu, and `GRAPH` opens Graph.
- In Expressions and Split, arrow keys select a line. `ENTER` starts or commits editing, `DEL` removes one character, and `CLEAR` erases the entire selected expression. In a menu, `CLEAR` goes back.
- In Graph, arrows pan, `+` zooms in, `-` zooms out, and `CLEAR` is ignored. Graph and Split draw an 8-pixel coarse pass, then a 4-pixel fine pass after navigation is released. New input cancels unfinished rendering. Viewport changes alone do not mark the project dirty.
- Press `2nd`, then `MODE` (`QUIT`) to leave DGCALC from any app screen. Unsaved work still requires Save, Discard, or Cancel.
- Number keys, arithmetic keys, parentheses, comma, `x²`, `^`, `sin`, `cos`, `tan`, `ln`, and `log` insert their displayed math.
- `X,T,θ,n` inserts `x`. `ALPHA` followed by the calculator’s printed letter keys inserts `a` through `z`; this includes `ALPHA`+`1` for `y` and `ALPHA`+`2` for `z`.
- `MATH` inserts `=`. `2nd`+`MATH` inserts `<`, `2nd`+`APPS` inserts `>`, `2nd`+`PRGM` inserts `<=`, and `2nd`+`VARS` inserts `>=`.
- `2nd`+`x²` inserts `sqrt(`; `2nd`+`sin`, `cos`, and `tan` insert `abs(`, `exp(`, and `floor(`.
- The `TRACE` project menu contains New, Save, Load, 2D/3D Mode, and Quit. Press `TRACE` again or `CLEAR` to close it without choosing anything. Unsaved New, Load, and Quit operations require Save, Discard, or Cancel.

Projects are stored in the archived AppVar `DGCPROJ`. Saves use `DGCTMP` plus `DGCBAK` so a failed write, validation, rename, or archive step can restore the previous project.
Version 1 projects load with the default viewport; version 2 saves preserve expressions and the current viewport.

## Verification record

- CEdev: v15.0, eZ80 Clang 19.1.0
- arTIfiCE Shell target: 2.1
- Calculator OS version: pending physical-calculator check
- Program size: 46,412-byte private `.8xp` (Milestone 2A development build)
- CE static render controller: 850 bytes, including one 846-byte project scheduler (from generated assembly); total linked BSS: 13,390 bytes
- Host checks: 54,552 checks, zero failures; CE build has no C warnings and only the two known GNU-stack linker warnings
- Physical speed, RAM, appearance, and calculator OS version remain pending; automated checks do not establish those measurements

## Pending private calculator checks

- [ ] Plot `y=x` and `x=2` in Graph and Split; verify grid, axes, labels, clipping, and distinct colors.
- [ ] Plot circle `x^2+y^2=25`, ellipse `9x^2+4y^2=36`, and reversed circle `25=x^2+y^2`.
- [ ] Plot `y=1/x`; verify no false bridge through the asymptote.
- [ ] Pan and zoom; verify responsive controls, coarse drawing, fine refinement after release, and cancellation of old geometry.
- [ ] Open/close the menu during rendering, switch views/modes, try ignored Graph `CLEAR`, and quit with `2nd`, then `MODE`; verify Save/Discard/Cancel and invalid-draft preservation.
- [ ] Load a version 1 project; save/load version 2 expressions and viewport.
- [ ] Measure free RAM and usability with 0, 1, 5, and 16 equations.
- [ ] Repeat clear/re-edit cycles and verify RAM recovery after clearing all lines.
