# DGCALC — private calculator build

DGCALC is an original offline graphing-calculator project for the TI-84 Plus CE Python. It uses a general expression engine rather than factory-style `Y=` slots, so the math foundation accepts relations such as `25=x^2+y^2` and `x^2+y^2+z^2=16` in either orientation.

This repository is still a private development build. Do not publish or package `DGCALC.8xp` yet. Milestone 1 supplies expression editing, parsing, validation, definitions, functions, project saving, and the three approved views. Actual 2D and 3D plotting arrives in later milestones; the current graph panels are labeled placeholders.

Milestone 1 caches up to 16 expression lines so the parser trees and recoverable-save buffer fit the calculator's usable RAM. The expression grammar still allows 128 AST nodes per line. A pooled AST cache is the planned route to a higher line count before final release.

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

- `Y=` opens Expressions, `MODE` opens the compact Split view, and `GRAPH` opens Graph.
- Arrow keys select a line. `ENTER` starts or commits editing; `CLEAR` cancels an edit and quits only when not editing; `DEL` backspaces.
- Number keys, arithmetic keys, parentheses, comma, `x²`, `^`, `sin`, `cos`, `tan`, `ln`, and `log` insert their displayed math.
- `X,T,θ,n` inserts `x`. `ALPHA` followed by the calculator’s printed letter keys inserts `a` through `z`; this includes `ALPHA`+`1` for `y` and `ALPHA`+`2` for `z`.
- `MATH` inserts `=`. `2nd`+`MATH` inserts `<`, `2nd`+`APPS` inserts `>`, `2nd`+`PRGM` inserts `<=`, and `2nd`+`VARS` inserts `>=`.
- `2nd`+`x²` inserts `sqrt(`; `2nd`+`sin`, `cos`, and `tan` insert `abs(`, `exp(`, and `floor(`.
- Outside an active edit, `PRGM` opens New, Save, Load, 2D/3D Mode, and Quit. Unsaved New, Load, and Quit operations require Save, Discard, or Cancel.

Projects are stored in the archived AppVar `DGCPROJ`. Saves use `DGCTMP` plus `DGCBAK` so a failed write, validation, rename, or archive step can restore the previous project.

## Verification record

- CEdev: v15.0, eZ80 Clang 19.1.0
- arTIfiCE Shell target: 2.1
- Calculator OS version: pending physical-calculator check
- Program size: 19,524-byte private `.8xp` (integrated Milestone 1 build)
- Physical checks: pending transfer to the user’s calculator
