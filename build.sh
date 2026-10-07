#!/usr/bin/env bash
#
# build.sh — build helper for the UltraGPS Arduino firmware.
#
#   ./build.sh build      Compile the firmware for both stations
#   ./build.sh upload     Compile and flash one station
#   ./build.sh monitor    Open a serial monitor on one station
#   ./build.sh clean      Remove the .pio build directories
#   ./build.sh dist       Collect the release artefacts into ./dist
#
# Both stations are separate PlatformIO projects; --station selects which one a
# command applies to.

set -euo pipefail

# Project root
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$PROJECT_DIR/dist"
SITE_DIR="$PROJECT_DIR/site"

# Station name -> project directory
BASE_PROJECT="base-station"
TX_PROJECT="transmitter-station"

STATION="all"
PORT=""

usage() {
    cat <<EOF
Usage: ./build.sh <command> [options]

Commands:
  build        Compile the firmware (both stations by default)
  upload       Compile and flash a station (requires --station)
  monitor      Open a 38400 bps serial monitor on a station (requires --station)
  clean        Remove the .pio build directories
  dist         Build both stations and collect the release artefacts
               (.hex images + a zip of the sketch sources) into ./dist
  docs         Preview the documentation locally with live reload
               (mkdocs serve — http://127.0.0.1:8000, Ctrl-C to stop)
  docs-build   Build the static documentation site into ./site

Options:
  -s, --station <name>   base | transmitter | all   (default: all)
  -p, --port <device>    Override the port from platformio.ini
                         (upload and monitor only)
  -h, --help             Show this help

Examples:
  ./build.sh build                      # compile both stations
  ./build.sh build -s base              # compile the base station only
  ./build.sh upload -s base             # flash the base station
  ./build.sh upload -s transmitter -p /dev/ttyACM2
  ./build.sh monitor -s base            # 38400 bps serial monitor
  ./build.sh clean                      # wipe both .pio directories
  ./build.sh dist                       # release artefacts in ./dist
  ./build.sh docs                       # live-preview docs without pushing
EOF
}

# Ensure PlatformIO is available before running a firmware command.
require_pio() {
    if ! command -v pio >/dev/null 2>&1; then
        echo "error: 'pio' (PlatformIO Core) is not installed." >&2
        echo "Install it with:" >&2
        echo "    python3 -m venv ~/.venvs/platformio" >&2
        echo "    ~/.venvs/platformio/bin/pip install platformio" >&2
        echo "    export PATH=\"\$HOME/.venvs/platformio/bin:\$PATH\"" >&2
        exit 1
    fi
}

# Ensure mkdocs is available before running a docs command; print an install
# hint otherwise.
require_mkdocs() {
    if ! command -v mkdocs >/dev/null 2>&1; then
        echo "error: 'mkdocs' is not installed." >&2
        echo "Install the documentation dependencies with:" >&2
        echo "    pip install -r docs/requirements.txt" >&2
        exit 1
    fi
}

# Echo the project directories selected by --station.
selected_projects() {
    case "$STATION" in
        base)         echo "$BASE_PROJECT" ;;
        transmitter)  echo "$TX_PROJECT" ;;
        all)          echo "$BASE_PROJECT $TX_PROJECT" ;;
        *) echo "error: unknown station '$STATION' (base|transmitter|all)" >&2
           exit 1 ;;
    esac
}

# upload and monitor act on exactly one board — refuse to guess.
require_single_station() {
    if [[ "$STATION" == "all" ]]; then
        echo "error: '$1' needs a single station: -s base | -s transmitter" >&2
        exit 1
    fi
}

cmd_build() {
    require_pio
    for project in $(selected_projects); do
        echo ">> Building $project ..."
        pio run -d "$PROJECT_DIR/$project"
        echo ">> Done: $project/.pio/build/uno/firmware.hex"
    done
}

cmd_upload() {
    require_pio
    require_single_station upload
    local project
    project="$(selected_projects)"
    echo ">> Uploading $project ..."
    if [[ -n "$PORT" ]]; then
        pio run -d "$PROJECT_DIR/$project" --target upload --upload-port "$PORT"
    else
        pio run -d "$PROJECT_DIR/$project" --target upload
    fi
}

cmd_monitor() {
    require_pio
    require_single_station monitor
    local project
    project="$(selected_projects)"
    echo ">> Monitoring $project at 38400 bps (Ctrl-C to stop) ..."
    if [[ -n "$PORT" ]]; then
        pio device monitor -d "$PROJECT_DIR/$project" -p "$PORT" -b 38400
    else
        pio device monitor -d "$PROJECT_DIR/$project" -b 38400
    fi
}

cmd_dist() {
    require_pio
    local version
    version="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION")"

    echo ">> Building both stations ..."
    pio run -d "$PROJECT_DIR/$BASE_PROJECT"
    pio run -d "$PROJECT_DIR/$TX_PROJECT"

    mkdir -p "$DIST_DIR"
    cp "$PROJECT_DIR/$BASE_PROJECT/.pio/build/uno/firmware.hex" \
       "$DIST_DIR/ultragps-base-station-v${version}.hex"
    cp "$PROJECT_DIR/$TX_PROJECT/.pio/build/uno/firmware.hex" \
       "$DIST_DIR/ultragps-transmitter-station-v${version}.hex"

    # The sketch sources, straight from git so only tracked files are included
    # and no build output leaks in.
    echo ">> Packaging the sketch sources ..."
    ( cd "$PROJECT_DIR" && git archive --format=zip \
        --prefix="ultragps-arduino-v${version}/" \
        -o "$DIST_DIR/ultragps-arduino-sources-v${version}.zip" \
        HEAD "$BASE_PROJECT" "$TX_PROJECT" shared README.md VERSION )

    echo ">> Done:"
    ls -1 "$DIST_DIR"
}

cmd_docs() {
    require_mkdocs
    echo ">> Serving docs at http://127.0.0.1:8000  (Ctrl-C to stop) ..."
    ( cd "$PROJECT_DIR" && mkdocs serve )
}

cmd_docs_build() {
    require_mkdocs
    echo ">> Building static docs into $SITE_DIR ..."
    ( cd "$PROJECT_DIR" && mkdocs build --site-dir "$SITE_DIR" )
    echo ">> Done: $SITE_DIR/index.html"
}

cmd_clean() {
    local cleaned=0
    for project in $(selected_projects); do
        if [[ -d "$PROJECT_DIR/$project/.pio" ]]; then
            echo ">> Removing $project/.pio ..."
            rm -rf "${PROJECT_DIR:?}/$project/.pio"
            cleaned=1
        fi
    done
    if [[ -d "$DIST_DIR" ]]; then
        echo ">> Removing dist ..."
        rm -rf "$DIST_DIR"
        cleaned=1
    fi
    if [[ "$cleaned" -eq 1 ]]; then
        echo ">> Clean."
    else
        echo ">> Nothing to clean."
    fi
}

# ---- argument parsing -------------------------------------------------------
COMMAND=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        build|upload|monitor|clean|dist|docs|docs-build)  COMMAND="$1" ;;
        -s|--station)  STATION="${2:-}"; shift ;;
        -p|--port)     PORT="${2:-}"; shift ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; echo >&2; usage >&2; exit 1 ;;
    esac
    shift
done

case "$COMMAND" in
    build)      cmd_build ;;
    upload)     cmd_upload ;;
    monitor)    cmd_monitor ;;
    clean)      cmd_clean ;;
    dist)       cmd_dist ;;
    docs)       cmd_docs ;;
    docs-build) cmd_docs_build ;;
    "")         usage >&2; exit 1 ;;
esac
