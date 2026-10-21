#!/usr/bin/env bash
# Build, test, validate and deploy SailRSS with the Sailfish SDK.
#
# Usage: scripts/dev.sh <command>
#   build      build the RPM for the target (runs the unit tests too)
#   test       rerun the unit tests on the last build
#   live-test  run the API tests against a FreshRSS server (see below)
#   check      run the Jolla Store (Harbour) validator on the built RPM
#   deploy     install the built RPM on the device
#   undeploy   remove the app from the device
#   lupdate    refresh translations/*.ts from the QML and C++ sources
#   all        build, check and deploy
#
# Override defaults with environment variables:
#   SFDK, SFDK_TARGET, SFDK_DEVICE
# live-test reads SAILRSS_URL, SAILRSS_USER, SAILRSS_PASSWORD and optionally
# SAILRSS_LIVE_WRITE=1 (development only, to be replaced in phase 4).
set -euo pipefail

SFDK=${SFDK:-$HOME/SailfishOS/bin/sfdk}
SFDK_TARGET=${SFDK_TARGET:-SailfishOS-5.1.0.11-aarch64}
SFDK_DEVICE=${SFDK_DEVICE:-Jolla Phone (2026) (ARM 64bit)}

cd "$(dirname "$0")/.."
ROOT=$PWD
# Out-of-tree build, one directory per architecture
BUILD_DIR=$ROOT/build/${SFDK_TARGET##*-}

sfdk() {
    "$SFDK" -c target="$SFDK_TARGET" -c device="$SFDK_DEVICE" "$@"
}

in_build_dir() {
    if [[ ! -f $BUILD_DIR/CMakeCache.txt ]]; then
        echo "No build in $BUILD_DIR, run '$0 build' first" >&2
        exit 1
    fi
    cd "$BUILD_DIR"
}

latest_rpm() {
    local rpm
    rpm=$(ls -t "$BUILD_DIR"/RPMS/harbour-sailrss-[0-9]*.rpm 2>/dev/null | head -n 1)
    if [[ -z $rpm ]]; then
        echo "No RPM found in $BUILD_DIR/RPMS, run '$0 build' first" >&2
        exit 1
    fi
    echo "$rpm"
}

cmd_build() {
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    sfdk build "$ROOT"
}
cmd_test() {
    in_build_dir
    sfdk build-shell ctest --output-on-failure
}
cmd_live_test() {
    in_build_dir
    local name missing=()
    for name in SAILRSS_URL SAILRSS_USER SAILRSS_PASSWORD; do
        [[ -n ${!name:-} ]] || missing+=("$name")
    done
    if (( ${#missing[@]} )); then
        echo "Not set or empty: ${missing[*]}" >&2
        echo "(In fish, quote values with special characters: set -x SAILRSS_PASSWORD 'secret')" >&2
        exit 1
    fi
    # sfdk does not forward environment variables, and command line
    # arguments are visible to other processes, so pass them on stdin.
    printf '%s\n' "$SAILRSS_URL" "$SAILRSS_USER" "$SAILRSS_PASSWORD" "${SAILRSS_LIVE_WRITE:-}" |
        sfdk build-shell bash -c '
            IFS= read -r SAILRSS_URL; IFS= read -r SAILRSS_USER
            IFS= read -r SAILRSS_PASSWORD; IFS= read -r SAILRSS_LIVE_WRITE
            export SAILRSS_URL SAILRSS_USER SAILRSS_PASSWORD SAILRSS_LIVE_WRITE
            exec tests/tst_greaderlive'
}
cmd_check() { sfdk check -s harbour "$(latest_rpm)"; }
cmd_deploy() {
    in_build_dir
    sfdk deploy --sdk
}
cmd_undeploy() {
    in_build_dir
    sfdk undeploy --sdk
}
cmd_lupdate() {
    "$SFDK" engine exec sb2 -t "$SFDK_TARGET.default" \
        lupdate "$ROOT/qml" "$ROOT/src" -no-obsolete \
        -ts "$ROOT"/translations/*.ts
}
cmd_all() { cmd_build && cmd_check && cmd_deploy; }

case ${1:-} in
    build|test|check|deploy|undeploy|lupdate|all) "cmd_$1" ;;
    live-test) cmd_live_test ;;
    *) sed -n '2,17s/^# \{0,1\}//p' "$0"; exit 1 ;;
esac
