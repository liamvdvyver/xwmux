# xwmux

The **X** **W**indow **Mu**ltiple**x**er

Use tmux as your window manager.

## Requirements

Requires `libX11`, and a c++ toolchain (including `cmake`) to build.
Requires `tmux`, `xorg` server, and one of the following terminals to run:

* `kitty` (all other considered experimental)
* `alacritty`
* `st`
* `xterm`

## Installation

* Run `cmake . && sudo make install`
* Add the following line to your `tmux.conf`: `run-shell 'xwmux-tmux-conf.sh'`
* Ensure your terminal is configured to use zero padding/borders on the
  top-left.

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

The following commands are supported (for the end user):
* `xwmux-ctl exit`: exit the session.

## Configuration

Set the environment variable `XWMUX_TERMINAL`, or `TERMINAL` to one of the supported options, otherwise first available is used.
As mentioned, keys bound in the prefix table are accessible from x windows.
To bind keys in other tables (e.g. with `bind-key -n`), use a hotkey daemon like `sxhkd`.

## TODO

Still in early development. Not currently supported:
* Desktop entries for display managers
* RandR (multi-monitor) setups
* System tray
* Very limited ICCCM/EWMH support
* Live zooming
