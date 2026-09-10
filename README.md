# xwmux

The **X** **W**indow **Mu**ltiple**x**er

Use tmux as your window manager.

For implementation details, see the [architecture and event-flow documentation](docs/README.md).

## Requirements

Requires `libX11`, and a c++ toolchain (including `cmake`) to build.
Requires `tmux`, `xorg` server, and one of the following terminals to run:

* `kitty` (all other considered experimental)
* `alacritty`
* `st`
* `xterm`

## Installation

* Run `cmake . && sudo make install`
* Ensure your terminal is configured to use zero padding/borders on the
  top-left.

No tmux configuration is required: the root-terminal initializer installs the
required hooks on the tmux server when xwmux starts. If you use a dedicated
tmux server, export `XWMUX_TMUX_SOCKET` before starting xwmux. When set,
xwmux and its helper scripts use `tmux -S "$XWMUX_TMUX_SOCKET" ...` instead of
the default tmux server.

## Usage

### xwmux

* Launch `xwmux` with using `startx`.
* Opened windows receive their own tmux split pane.
* Keys are sent to x windows when the corresponding pane gets focus.
* The prefix is always sent to the terminal/tmux window. To send it to the x window instead, type it again.
* X windows are killed when the pane is killed.
* To reload your terminal layout (e.g. after zooming), or tmux prefix,
  close your terminal (by disconnecting from the tmux session).

### xwmux-ctl

Use the `xwmux-ctl` program to control xwmux.

Run `xwmux-ctl --help` to see all commands.

The following commands are supported (for the end user):
* `xwmux-ctl exit`: exit the session.
* `xwmux-ctl dump-state`: print the current tmux/X window mapping state as JSON.

## Configuration

### Environment variables

* `XWMUX_TERMINAL`: terminal emulator used for the root terminal. Supported
  values are `kitty`, `alacritty`, `st`, and `xterm`.
* `TERMINAL`: fallback terminal selection if `XWMUX_TERMINAL` is unset.
* `XWMUX_TMUX_SOCKET`: path to the tmux socket xwmux should use. When set,
  all WM-side tmux interaction and the helper scripts use that socket via
  `tmux -S <socket> ...`. This is useful for isolated test setups and for
  running xwmux against a dedicated tmux server.

If neither `XWMUX_TERMINAL` nor `TERMINAL` is set, xwmux picks the first
available supported terminal in this order:
`kitty`, `alacritty`, `st`, `xterm`.

### tmux options used by xwmux

xwmux reads the following tmux options from the connected tmux server:

* `prefix`: used to determine which key should be intercepted and forwarded to
  tmux.
* `status`: used to detect whether the tmux status bar is enabled.
* `status-position`: used to compute layout correctly when the tmux status bar
  is at the top or bottom.

### Hooks

The root-terminal initializer installs the tmux hooks xwmux needs to track pane
focus, pane creation/destruction, window changes, and layout updates. It also
enables tmux `focus-events`. No `~/.tmux.conf` changes are needed.

`xwmux-tmux-conf.sh` remains as a compatibility entry point for existing
configurations; it only registers those same hooks and is not needed for new
installations.

### Test launcher

For nested testing, `src/scripts/xwmux-xephyr-test.sh` starts xwmux inside
Xephyr with:

* a unique tmux socket
* a dedicated tmux server
* a minimal clean tmux config from `src/scripts/xwmux-test.tmux.conf`

This avoids interference from your usual tmux session and `~/.tmux.conf`.

### Testing sync plan

Current integration tests sometimes need to wait for tmux hooks and WM event
handling to settle before making assertions. A proposed improvement is to add a
small explicit sync primitive to the IPC layer:

* add a client-to-WM `sync` request
* have the WM reply by updating a sequence number / response payload
* have tmux hook scripts issue that sync request after reporting layout/focus
  changes
* let tests wait for the sequence number to advance before asserting on focus or
  state

That would provide a deterministic way to know that hook-triggered updates have
been processed, instead of relying on sleeps/polling alone.

As mentioned, keys bound in the prefix table are accessible from x windows.
To bind keys in other tables (e.g. with `bind-key -n`), use a hotkey daemon like `sxhkd`.

## TODO

Still in early development. Not currently supported:
* Desktop entries for display managers
* RandR (multi-monitor) setups
* System tray
* Very limited ICCCM/EWMH support
* Live zooming
