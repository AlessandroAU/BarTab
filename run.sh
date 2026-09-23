#!/bin/sh
# Start the installed build, replacing any running instance, as run.bat does on Windows.
# Arguments pass through: ./run.sh --reset starts from the default settings.
set -eu
root="$(cd "$(dirname "$0")" && pwd)"
widget="$root/bin/UsageTracker"
if [ ! -x "$widget" ]; then
    echo "Widget executable not found: $widget" >&2
    echo "Run ./build.sh first." >&2
    exit 1
fi
pkill -f '/UsageTracker( |$)' 2>/dev/null || true
cd "$root/bin"
nohup "$widget" "$@" >/dev/null 2>&1 &
