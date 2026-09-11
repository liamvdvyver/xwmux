# xwmux

xwmux is an X11 window manager whose GUI-client layout and lifecycle are modeled by tmux panes.

## Language

**Mapped GUI client**:
An X11 client window associated with one tmux pane in the `TmuxXWindowMapping`.
_Avoid_: GUI pane, managed window

**Active location**:
The `(tmux window, pane)` selected by the tmux client and most recently applied by xwmux.
_Avoid_: focused pane, current pane

**Visible workspace**:
The mapped GUI clients in the tmux window currently applied by xwmux; clients outside it are hidden and cannot be pointer-selected. Cross-workspace pointer selection is deferred until xwmux has a suitable session-aware model.
_Avoid_: desktop, active session

**Prefix override**:
The state in which a mapped GUI client remains the active location while xwmux intentionally gives X input focus to the root terminal so tmux can consume a prefix sequence.
_Avoid_: focus transition, focus lockout

**Pointer selection**:
An external X `FocusIn` on a visible mapped GUI client; it selects that client’s pane in tmux and cancels any active tmux prefix sequence. `FocusOut` alone has no tmux effect.
_Avoid_: click focus, inverse focus

**xwmux tmux option**:
A global tmux user option in the `@xwmux-*` namespace that configures xwmux through its connected tmux server. `@xwmux-terminal` selects a supported root terminal; `@xwmux-terminal-command` is reserved for a future arbitrary-command setting.
_Avoid_: xwmux environment variable, WM setting
