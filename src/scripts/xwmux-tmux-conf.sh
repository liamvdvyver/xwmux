#!/usr/bin/env sh

# Backward-compatible entry point for configurations that used to source this
# script. xwmux-init-term.sh now installs the hooks automatically.
script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
exec "$script_dir/xwmux-init-term.sh" --configure-hooks
