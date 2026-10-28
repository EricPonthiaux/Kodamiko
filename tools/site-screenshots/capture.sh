#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Starts the real Kodamiko offscreen on a fresh clone of this repository, with
# SceneDriver.cpp linked in, so that the site's screenshots can be set up and
# grabbed by writing commands to <work>/shots/command (see SceneDriver.cpp).
#
#   tools/site-screenshots/capture.sh <work-folder> [scale]
#
# <work-folder> is deleted first: give it a folder of its own. The scale is 1
# or 2 (the -2x images). The build folder is build-debug, built beforehand.
#
# Kodamiko runs with its own HOME and configuration: nothing of the user's
# settings is read or written. Claude sessions use the user's real Claude
# installation and account, through a wrapper that restores XDG_DATA_HOME and
# XDG_CONFIG_HOME: with an isolated XDG_DATA_HOME, Claude's native installer
# would repoint ~/.local/bin/claude into the work folder.
set -euo pipefail

work=${1:?work folder}
scale=${2:-1}
repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
build=$repo/build-debug
screenWidth=1600
screenHeight=1000

rm -rf "$work"
mkdir -p "$work"/{home,config,data,cache,bin,shots}
work=$(cd "$work" && pwd)

# --- the kodamiko executable, with the scene driver ----------------------------
compileMain=$(jq -r '.[] | select(.file | endswith("src/app/main.cpp")) | .command' "$build/compile_commands.json")
compileDriver=${compileMain/ -o * -c */ -o $work/SceneDriver.o -c $repo/tools/site-screenshots/SceneDriver.cpp}
qtWidgets=$(grep -o -- '-isystem [^ ]*/QtWidgets' <<< "$compileMain" | head -1)
compileDriver="$compileDriver ${qtWidgets%Widgets}Test"
(cd "$build" && eval "$compileDriver")
linkKDK=$(ninja -C "$build" -t commands bin/kodamiko | tail -1)
linkKDK=${linkKDK/ -o bin\/kodamiko / -o $work/bin/kodamiko $work/SceneDriver.o }
qtCore=$(grep -o -- '[^ ]*libQt6Core.so[^ ]*' <<< "$linkKDK" | head -1)
linkKDK=${linkKDK/ && :/ ${qtCore/Core/Test} && :}
(cd "$build" && eval "${linkKDK#: && }")
# Kodamiko finds kodamiko-mcp, kodamiko-hook and the recipes beside its executable.
for helper in "$build"/bin/*; do
    [[ $(basename "$helper") == kodamiko ]] || ln -s "$helper" "$work/bin/"
done

# --- the project: a clone, never the working tree --------------------------
git clone -q "$repo" "$work/Kodamiko"
# clangd reads the clone's sources, and the headers generated in the build.
sed -e "s#$build#@build@#g" -e "s#$repo/#$work/Kodamiko/#g" -e "s#@build@#$build#g" \
    "$build/compile_commands.json" > "$work/Kodamiko/compile_commands.json"

# --- Claude, with the user's own data folders ------------------------------
cat > "$work/bin/claude" <<EOF
#!/usr/bin/env bash
unset XDG_DATA_HOME XDG_CONFIG_HOME XDG_CACHE_HOME
export HOME=$HOME
exec $HOME/.local/bin/claude "\$@"
EOF
chmod +x "$work/bin/claude"

# --- settings: defaults, the window filling the screen ---------------------
mkdir -p "$work/config/Kodamiko"
cp "$(dirname "$0")/Kodamiko.conf" "$work/config/Kodamiko/Kodamiko.conf"
cat > "$work/screen.json" <<EOF
{ "screens": [ { "name": "shots", "x": 0, "y": 0, "width": $((screenWidth * scale)), "height": $((screenHeight * scale)),
                 "logicalDpi": 96, "logicalBaseDpi": 96 } ] }
EOF

env -i \
    HOME="$work/home" \
    XDG_CONFIG_HOME="$work/config" \
    XDG_DATA_HOME="$work/data" \
    XDG_CACHE_HOME="$work/cache" \
    PATH="$work/bin:/usr/local/bin:/usr/bin:/bin" \
    LANG=en_US.UTF-8 \
    TERM=xterm-256color \
    QT_SCALE_FACTOR="$scale" \
    QT_QPA_PLATFORM="offscreen:configfile=$work/screen.json" \
    KDK_SHOTS="$work/shots" \
    "$work/bin/kodamiko" "$work/Kodamiko" > "$work/kodamiko.log" 2>&1
