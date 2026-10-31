#!/bin/sh
# Convenience flash script for this SquareLine-exported ESP-IDF project.
# You can always flash directly with:  idf.py -p <PORT> flash monitor
#
# Usage:  ./flash.sh                        - auto-detect the serial port
#         ./flash.sh /dev/cu.usbserial-10   - use that port
#         ./flash.sh -p /dev/ttyUSB0        - any idf.py arguments are passed through
# You can also set ESPPORT instead of passing the port every time.

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
    | head -n 1 | tr -d '"')
[ -n "$IDF_VER" ] && echo "ESP-IDF in use : $IDF_VER"
[ -n "$IDF_PIN" ] && echo "Project needs  : ESP-IDF $IDF_PIN"
echo

# Work out the port: explicit argument wins, then ESPPORT, then auto-detect.
# An argument starting with "-" is treated as raw idf.py arguments instead.
PORT=""
PASSTHRU=""
case "${1:-}" in
    "") ;;
    -*) PASSTHRU="$*" ;;
    *)  PORT="$1" ;;
esac
if [ -z "$PASSTHRU" ] && [ -z "$PORT" ] && [ -n "${ESPPORT:-}" ]; then
    PORT="$ESPPORT"
fi

if [ -n "$PASSTHRU" ]; then
    # shellcheck disable=SC2086
    idf.py $PASSTHRU flash monitor
elif [ -n "$PORT" ]; then
    echo "Using serial port: $PORT"
    echo
    idf.py -p "$PORT" flash monitor
else
    echo "No serial port given - letting idf.py auto-detect it."
    echo
    idf.py flash monitor
fi
status=$?

if [ "$status" -ne 0 ]; then
    echo
    echo "------------------------------------------------------------------"
    echo "Flashing failed."
    echo
    if [ -z "$PORT" ] && [ -z "$PASSTHRU" ]; then
        echo "The port was auto-detected. If the wrong one was picked, pass it"
        echo "explicitly, for example:"
        echo
        echo "    ./flash.sh /dev/cu.usbserial-10"
        echo
        echo "or set ESPPORT before running this script."
        echo
    fi
    [ -n "$PORT" ] && echo "The port used was $PORT. If that is not your board, try another one."
    echo
    echo "Serial ports currently present on this machine:"
    found=$(ls /dev/cu.* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null)
    if [ -n "$found" ]; then
        echo "$found" | sed 's/^/    /'
    else
        echo "    (none found - is the board plugged in and the USB driver installed?)"
    fi
    echo
    echo "If the error above mentions \"version solving failed\", the ESP-IDF you are"
    echo "building with does not match what this project requires:"
    [ -n "$IDF_VER" ] && echo "  ESP-IDF in use : $IDF_VER"
    [ -n "$IDF_PIN" ] && echo "  Project needs  : ESP-IDF $IDF_PIN"
    echo "------------------------------------------------------------------"
fi

exit "$status"
