#!/usr/bin/env bash
# Build, test, validate and deploy SailRSS with the Sailfish SDK.
#
# Usage: scripts/dev.sh <command>
#   build             build the RPM for the target (runs the unit tests too)
#   test              rerun the unit tests on the last build
#   test-server       start a throwaway FreshRSS in Podman for live-test
#   test-server-stop  stop and delete it
#   live-test         run the API and sync tests against that test server
#   check             run the Jolla Store (Harbour) validator on the built RPM
#   release           build the store package (stripped, debug info apart) and check it
#   deploy            install the built RPM on the device
#   undeploy          remove the app from the device
#   lupdate           refresh translations/*.ts from the QML and C++ sources
#   all               build, check and deploy
#
# Override defaults with environment variables:
#   SFDK, SFDK_TARGET, SFDK_DEVICE
# The live tests change data freely and only ever run against the test
# server, never a real account.
set -euo pipefail

SFDK=${SFDK:-$HOME/SailfishOS/bin/sfdk}
SFDK_TARGET=${SFDK_TARGET:-SailfishOS-5.1.0.11-aarch64}
SFDK_DEVICE=${SFDK_DEVICE:-Jolla Phone (2026) (ARM 64bit)}

# Throwaway test server. Its credentials protect nothing.
TEST_CONTAINER=sailrss-freshrss
TEST_IMAGE=docker.io/freshrss/freshrss:1.30.3
TEST_PORT=8089
TEST_USER=tester
TEST_PASSWORD=apitest123

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
# The tests run inside the SDK build engine, which reaches the host through
# the gateway of its Docker network.
engine_gateway() {
    "$SFDK" engine start >/dev/null 2>&1 || true
    docker network inspect "sailfish-sdk:$USER" --format '{{range .IPAM.Config}}{{.Gateway}}{{end}}'
}

test_api() {
    local auth=$1 path=$2
    shift 2
    curl -fsS -H "Authorization: GoogleLogin auth=$auth" "$@" "http://127.0.0.1:$TEST_PORT/api/greader.php$path"
}

cmd_test_server() {
    if podman container exists "$TEST_CONTAINER"; then
        podman start "$TEST_CONTAINER" >/dev/null
        echo "Test server already exists, started it"
        return
    fi
    local gateway
    gateway=$(engine_gateway)
    # Listens only on localhost and the SDK network, not on the LAN.
    podman run -d --name "$TEST_CONTAINER" \
        -p "127.0.0.1:$TEST_PORT:80" -p "$gateway:$TEST_PORT:80" \
        -e FRESHRSS_INSTALL="--api-enabled --default-user $TEST_USER" \
        -e FRESHRSS_USER="--user $TEST_USER --password webtest123 --api-password $TEST_PASSWORD --email $TEST_USER@example.net" \
        "$TEST_IMAGE" >/dev/null
    local i
    for i in $(seq 30); do
        curl -fs -o /dev/null "http://127.0.0.1:$TEST_PORT/api/greader.php" && break
        sleep 1
    done

    # Seed: FreshRSS adds its release feed itself; add two more, one in a
    # folder, and star the two oldest articles.
    local auth token ids
    auth=$(curl -fsS -d "Email=$TEST_USER&Passwd=$TEST_PASSWORD" \
        "http://127.0.0.1:$TEST_PORT/api/greader.php/accounts/ClientLogin" | sed -n 's/^Auth=//p')
    token=$(test_api "$auth" /reader/api/0/token)
    test_api "$auth" /reader/api/0/subscription/quickadd -d "T=$token" \
        --data-urlencode quickadd=https://blog.jolla.com/feed/ >/dev/null
    test_api "$auth" /reader/api/0/subscription/quickadd -d "T=$token" \
        --data-urlencode quickadd=https://forum.sailfishos.org/latest.rss >/dev/null
    test_api "$auth" /reader/api/0/subscription/edit -d "T=$token" -d ac=edit -d s=feed/2 \
        --data-urlencode a=user/-/label/Sailfish >/dev/null
    ids=$(test_api "$auth" "/reader/api/0/stream/items/ids?s=user/-/state/com.google/reading-list&n=1000&r=o&output=json" |
        grep -o '"id":"[0-9]*"' | head -n 2 | grep -o '[0-9]*' | sed 's/^/-d i=/' | tr '\n' ' ')
    # shellcheck disable=SC2086
    test_api "$auth" /reader/api/0/edit-tag -d "T=$token" -d a=user/-/state/com.google/starred $ids >/dev/null
    echo "Test server running on http://127.0.0.1:$TEST_PORT (user $TEST_USER)"
}

cmd_test_server_stop() {
    podman rm -f "$TEST_CONTAINER" >/dev/null && echo "Test server removed"
}

cmd_live_test() {
    in_build_dir
    if ! curl -fs -o /dev/null "http://127.0.0.1:$TEST_PORT/api/greader.php"; then
        echo "The test server is not running, start it with '$0 test-server'" >&2
        exit 1
    fi
    local url
    url=http://$(engine_gateway):$TEST_PORT
    # sfdk does not forward environment variables, so set them inside.
    sfdk build-shell env SAILRSS_TEST_URL="$url" SAILRSS_TEST_USER="$TEST_USER" \
        SAILRSS_TEST_PASSWORD="$TEST_PASSWORD" bash -c 'tests/tst_greaderlive && tests/tst_synclive'
}
cmd_check() { sfdk check -s harbour "$(latest_rpm)"; }
# With --enable-debug, rpm moves the debug info into separate -debuginfo and
# -debugsource packages and strips the app; only the main RPM is submitted.
cmd_release() {
    BUILD_DIR=$ROOT/build/release-${SFDK_TARGET##*-}
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    sfdk build --enable-debug "$ROOT"
    sfdk check -s harbour "$(latest_rpm)"
    echo "Store package: $(latest_rpm)"
}
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
    build|test|check|release|deploy|undeploy|lupdate|all) "cmd_$1" ;;
    live-test) cmd_live_test ;;
    test-server) cmd_test_server ;;
    test-server-stop) cmd_test_server_stop ;;
    *) sed -n '2,21s/^# \{0,1\}//p' "$0"; exit 1 ;;
esac
