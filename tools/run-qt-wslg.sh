#!/usr/bin/env bash
# Starts the portable front end on a Windows desktop through WSLg, which is how
# the Qt version can be looked at without a Linux machine or a Windows Qt build.
#
#   wsl -d Ubuntu -- bash tools/run-qt-wslg.sh
#
# Set CVB_QT to the SDK used for the build if it needs explicit runtime paths.
set -u
cd "$(dirname "$0")/.."

if [ -n "${CVB_QT:-}" ]; then
    sysroot=$HOME/qtsysroot/tree/usr/lib/x86_64-linux-gnu
    export LD_LIBRARY_PATH="$CVB_QT/lib:$sysroot${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QT_PLUGIN_PATH="$CVB_QT/plugins"
fi

export DISPLAY=${DISPLAY:-:0}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/mnt/wslg/runtime-dir}

exec ./build/linux/bin/CVBuilder-Qt "$@"
