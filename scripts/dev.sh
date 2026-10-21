#!/usr/bin/env bash
# Build, validate and deploy SailRSS with the Sailfish SDK.
#
# Usage: scripts/dev.sh <command>
#   build     build the RPM for the target
#   check     run the Jolla Store (Harbour) validator on the built RPM
#   deploy    install the built RPM on the device
#   undeploy  remove the app from the device
#   lupdate   refresh translations/*.ts from the QML and C++ sources
#   all       build, check and deploy
#
# Override defaults with environment variables:
#   SFDK, SFDK_TARGET, SFDK_DEVICE
set -euo pipefail

SFDK=${SFDK:-$HOME/SailfishOS/bin/sfdk}
SFDK_TARGET=${SFDK_TARGET:-SailfishOS-5.1.0.11-aarch64}
SFDK_DEVICE=${SFDK_DEVICE:-Jolla Phone (2026) (ARM 64bit)}

cd "$(dirname "$0")/.."
ROOT=$PWD

sfdk() {
    "$SFDK" -c target="$SFDK_TARGET" -c device="$SFDK_DEVICE" "$@"
}

latest_rpm() {
    local rpm
    rpm=$(ls -t RPMS/harbour-sailrss-[0-9]*.rpm 2>/dev/null | head -n 1)
    if [[ -z $rpm ]]; then
        echo "No RPM found in RPMS/, run '$0 build' first" >&2
        exit 1
    fi
    echo "$rpm"
}

cmd_build()    { sfdk build; }
cmd_check()    { sfdk check -s harbour "$(latest_rpm)"; }
cmd_deploy()   { sfdk deploy --sdk; }
cmd_undeploy() { sfdk undeploy --sdk; }
cmd_lupdate() {
    "$SFDK" engine exec sb2 -t "$SFDK_TARGET.default" \
        lupdate "$ROOT/qml" "$ROOT/src" -no-obsolete \
        -ts "$ROOT"/translations/*.ts
}
cmd_all() { cmd_build && cmd_check && cmd_deploy; }

case ${1:-} in
    build|check|deploy|undeploy|lupdate|all) "cmd_$1" ;;
    *) sed -n '2,13s/^# \{0,1\}//p' "$0"; exit 1 ;;
esac
