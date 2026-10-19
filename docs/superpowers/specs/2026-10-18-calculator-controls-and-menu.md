# Calculator Controls and Menu Design

## Goal

Make DGCALC's controls predictable on a TI-84 Plus CE and keep modal menus visually still.

## Approved controls

- `Y=` opens Expressions.
- `WINDOW` opens Compact split.
- `ZOOM` toggles 2D/3D.
- `TRACE` opens the project menu.
- `GRAPH` opens Graph.
- `DEL` removes one character while editing.
- `CLEAR` removes the entire selected expression with one press. In a modal menu, it goes back.
- `2nd`, then `MODE` (`QUIT`) requests exit from any app screen. Unsaved work still uses Save/Discard/Cancel protection.

## Rendering

Before every modal-dialog redraw, copy the visible GraphX screen into the draw buffer. This keeps the background fixed while the selected menu item changes.

## Constraints

- Keep the project private and create no shareable package.
- Add no dependency or general input abstraction.
- Preserve project-save safety and all existing expression syntax.
- Verify portable project-state behavior with host tests, calculator-only routing with CEdev compilation, and final behavior on the user's calculator.
