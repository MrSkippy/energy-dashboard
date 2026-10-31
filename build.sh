#!/bin/sh
# Convenience build script for this SquareLine-exported ESP-IDF project.
# You can always build directly with:  idf.py build
# (Run this from a shell where the ESP-IDF environment is activated, or let
#  the script activate it from IDF_PATH / the default install location.)

unset CDPATH
cd "$(dirname "$0")" || exit 1

# 1) Use the already-activated ESP-IDF environment if available
if ! command -v idf.py >/dev/null 2>&1; then
    # 2) Fall back to sourcing export.sh from IDF_PATH or common install locations
    for candidate in \
        "${IDF_PATH:-}" \
        "$HOME/esp/esp-idf" \
        "$HOME/esp/esp-idf-master" \
        "/opt/esp/esp-idf" \
        "/opt/esp/esp-idf-master" \
        "/Users/$USER/esp/esp-idf" \
        "/Users/$USER/esp/esp-idf-master"
    do
        if [ -n "$candidate" ] && [ -f "$candidate/export.sh" ]; then
            IDF_PATH="$candidate"
            export IDF_PATH
            echo "Activating ESP-IDF environment from $IDF_PATH"
            IDF_PATH_FORCE=1
            export IDF_PATH_FORCE
            . "$IDF_PATH/export.sh"
            unset IDF_PATH_FORCE
            break
        fi
    done
fi

if ! command -v idf.py >/dev/null 2>&1; then
    echo "Error: idf.py not found."
    echo "Activate the ESP-IDF environment first (e.g. '. \$IDF_PATH/export.sh' or the 'get_idf' alias),"
    echo "or set IDF_PATH to your ESP-IDF installation directory."
    echo "ESP-IDF install guide: https://docs.espressif.com/projects/esp-idf/en/stable/get-started/"
    exit 1
fi

# Show the ESP-IDF in use and what the project asks for, so a mismatch is
# visible up front instead of only as a CMake error deep in the output.
IDF_VER=$(idf.py --version 2>/dev/null | tail -n 1)
IDF_PIN=$(sed -n 's/^[[:space:]]*idf:[[:space:]]*//p' main/idf_component.yml 2>/dev/null \
    | head -n 1 | tr -d "\"'")
[ -n "$IDF_VER" ] && echo "ESP-IDF in use : $IDF_VER"
[ -n "$IDF_PIN" ] && echo "Project needs  : ESP-IDF $IDF_PIN"
echo

# Recover from an incomplete CMake configuration left behind by an earlier
# failed setup: build/ exists but CMake never finished writing its cache, so an
# incremental build would fail on a half-configured tree.
# sdkconfig is deliberately left alone - it holds the project configuration.
if [ -d build ] && [ ! -f build/CMakeCache.txt ]; then
    echo "Found a half-configured build/ directory from an earlier failed run."
    echo "Removing it so CMake can configure from scratch..."
    rm -rf build
fi

# Incremental build. Run 'idf.py fullclean' first if you need a clean build.
idf.py build
status=$?

if [ "$status" -ne 0 ]; then
    echo
    echo "------------------------------------------------------------------"
    echo "Build failed."
    echo
    echo "If the error above mentions \"version solving failed\", the ESP-IDF you"
    echo "are building with does not match what this project requires:"
    echo
    [ -n "$IDF_VER" ] && echo "  ESP-IDF in use : $IDF_VER"
    [ -n "$IDF_PIN" ] && echo "  Project needs  : ESP-IDF $IDF_PIN"
    echo
    echo "Activate a matching ESP-IDF version, or set IDF_PATH to point at one."
    echo
    echo "If you just switched ESP-IDF versions, the old build/ directory can also"
    echo "be the problem. Run 'idf.py fullclean' once and build again."
    echo "------------------------------------------------------------------"
fi

exit "$status"
