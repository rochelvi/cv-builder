#!/usr/bin/env bash
# Starts the portable front end on a Windows desktop through WSLg, which is how
# the Qt version can be looked at without a Linux machine or a Windows Qt build.
#
#   wsl -d Ubuntu -- bash tools/run-qt-wslg.sh
#
# Needs the Qt libraries on the loader path, since Qt here was unpacked into $HOME
# rather than installed.
set -u
cd "$(dirname "$0")/.."

qt=${CVB_QT:-$HOME/Qt/6.8.3/gcc_64}
sysroot=$HOME/qtsysroot/tree/usr/lib/x86_64-linux-gnu

export LD_LIBRARY_PATH="$qt/lib:$sysroot${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$qt/plugins"
export DISPLAY=${DISPLAY:-:0}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/mnt/wslg/runtime-dir}

# WSLg offers Wayland as well, but the wayland plugin lives in the qtwayland
# module, which a qtbase-only installation does not have. Asking for xcb outright
# is quieter than letting Qt try wayland first and complain.
if [ ! -f "$qt/plugins/platforms/libqwayland-generic.so" ]; then
    export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-xcb}
    unset WAYLAND_DISPLAY
fi

exec ./build/linux/bin/CVBuilder-Qt "$@"
