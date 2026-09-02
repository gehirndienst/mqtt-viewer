# mqtt-viewer build recipes
# Run `just` to see available commands
# Author: Nikita Smirnov <nktsmirnov@gmail.com> - https://github.com/gehirndienst

_default:
    @just --list

_compile dir *setup_args:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -d {{dir}} ]; then
        meson setup {{dir}} {{setup_args}}
    fi
    log=$(mktemp)
    trap 'rm -f "$log"' EXIT
    if ! meson compile -C {{dir}} 2>&1 | tee "$log"; then
        if grep -q "file not found" "$log"; then
            meson setup --wipe {{dir}}
            meson compile -C {{dir}}
        else
            exit 1
        fi
    fi

# Build the project. No flag build debugoptimized build, --debug builds debug build, --release builds release build
[arg("dbg", long="debug", value="debug")]
[arg("rel", long="release", value="release")]
build dbg='' rel='':
    #!/usr/bin/env bash
    set -euo pipefail
    if [ -n "{{dbg}}" ]; then
        just _compile builddir-debug --buildtype=debug
    elif [ -n "{{rel}}" ]; then
        just _compile builddir-release --buildtype=release
    else
        just _compile builddir --buildtype=debugoptimized
    fi

# Run all tests (uses the default build)
test:
    meson test -C builddir -v

# Run the built executable (debug or release if specified, otherwise default build)
[arg("dbg", long="debug", value="debug")]
[arg("rel", long="release", value="release")]
run dbg='' rel='':
    #!/usr/bin/env bash
    set -euo pipefail
    export MQTT_VIEWER_DB_PATH="${MQTT_VIEWER_DB_PATH:-mqtt_viewer.db}"
    if [ -n "{{dbg}}" ]; then
        just build --debug
        ./builddir-debug/mqtt-viewer
    elif [ -n "{{rel}}" ]; then
        just build --release
        ./builddir-release/mqtt-viewer
    else
        just build
        ./builddir/mqtt-viewer
    fi

# Run a single test suite by name (e.g. just test-one spsc_queue)
test-one name:
    meson test -C builddir -v --suite mqtt-viewer:{{name}}

# Generate API docs from header Doxygen comments (requires doxygen)
docs:
    doxygen Doxyfile
    @echo "Docs written to docs/doxygen/html/index.html"

# Remove all build artefacts and tear down the test environment
clean:
    just testenv-stop
    rm -rf builddir builddir-debug builddir-release builddir-release-static packaging/staging

# Download wrap dependencies into subprojects/packagecache/ (requires network)
download-deps:
    meson subprojects download --sourcedir .

# Run the debug build under a debugger (lldb on macOS, gdb on Linux)
dbgr:
    #!/usr/bin/env bash
    set -euo pipefail
    just build --debug
    if command -v lldb &>/dev/null; then
        lldb ./builddir-debug/mqtt-viewer
    elif command -v gdb &>/dev/null; then
        gdb ./builddir-debug/mqtt-viewer
    else
        echo "No debugger found. Install lldb (macOS) or gdb (Linux)." >&2
        exit 1
    fi

# Bump version, commit, and tag. Pass --push to push immediately.
[arg("version", long="version")]
[arg("push", long="push", value="yes")]
release version push='':
    #!/usr/bin/env bash
    set -euo pipefail
    if [[ -n "$(git status --porcelain)" ]]; then
        echo "Working tree is dirty - commit or stash changes first"
        exit 1
    fi
    meson rewrite -s . kwargs set project / version '{{version}}'
    git add meson.build
    git commit -m "bumped version to {{version}}"
    git tag -a "v{{version}}" -m "Release v{{version}}"
    if [[ -n "{{push}}" ]]; then
        git push && git push --tags
    else
        echo "Tagged v{{version}}. Push with: git push && git push --tags"
    fi

# Build a release package. --target macos (.dmg) or --target linux (.deb). Defaults to current OS
[arg("target", long="target")]
package target='':
    #!/usr/bin/env bash
    set -euo pipefail
    just _compile builddir-release-static --buildtype=release --wrap-mode=forcefallback --default-library=static
    case "{{target}}" in
        macos) bash packaging/build-dmg.sh ;;
        linux) bash packaging/build-deb.sh ;;
        '')
            if [[ "$(uname)" == "Darwin" ]]; then
                bash packaging/build-dmg.sh
            else
                bash packaging/build-deb.sh
            fi ;;
        *) echo "Unknown target '{{target}}'" >&2; exit 1 ;;
    esac

# Start a test broker on :1889, seed tests/env/test.db, publish sample traffic, then run the app against it (--only-env to skip the run)
[arg("only", long="only-env", value="yes")]
testenv only='':
    #!/usr/bin/env bash
    set -euo pipefail
    for tool in mosquitto mosquitto_pub sqlite3; do
        command -v "$tool" >/dev/null || { echo "$tool not found (install mosquitto + sqlite3 via brew or apt)" >&2; exit 1; }
    done
    mkdir -p tests/env
    if ! pgrep -f "mosquitto -p 1889" >/dev/null; then
        mosquitto -p 1889 -d
        sleep 0.3
    fi
    if [ ! -f tests/env/test.db ]; then
        sqlite3 tests/env/test.db <<'SQL'
    CREATE TABLE IF NOT EXISTS profiles (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL,
        host TEXT NOT NULL,
        port INTEGER DEFAULT 1883,
        protocol_version INTEGER DEFAULT 311,
        client_id TEXT DEFAULT '',
        clean_session INTEGER DEFAULT 1,
        keepalive_secs INTEGER DEFAULT 60,
        username TEXT DEFAULT '',
        password TEXT DEFAULT '',
        tls_ca_cert TEXT DEFAULT '',
        tls_client_cert TEXT DEFAULT '',
        tls_client_key TEXT DEFAULT '',
        tls_version INTEGER DEFAULT 13,
        tls_verify INTEGER DEFAULT 1,
        subscriptions TEXT DEFAULT '[{"topic":"#","qos":1}]'
    );
    INSERT INTO profiles (name, host, port, tls_version) VALUES ('testenv-local', 'localhost', 1889, 0);
    SQL
    fi
    pub() { mosquitto_pub -h localhost -p 1889 "$@"; }
    pub -t home/livingroom/light -r -m on
    pub -t home/kitchen/light -r -m off
    pub -t home/kitchen/fridge/door -m closed
    pub -t cars/car1/battery -m '{"soc":81,"health":"good"}'
    pub -t cars/car2/battery -m '{"soc":47,"health":"fair"}'
    if [ ! -f tests/env/pusher.pid ] || ! kill -0 "$(cat tests/env/pusher.pid)" 2>/dev/null; then
        (
            tick=0
            while :; do
                tick=$((tick + 1))
                pub -t sensors/temp -q 1 -m "{\"v\":$((20 + RANDOM % 8)).$((RANDOM % 10))}" || break
                pub -t sensors/hum -m "{\"v\":$((40 + RANDOM % 20))}" || break
                pub -t factory/line1/temp -m "{\"v\":$((60 + RANDOM % 15)).$((RANDOM % 10))}" || break
                pub -t factory/line1/rpm -m "$((1400 + RANDOM % 200))" || break
                pub -t factory/line2/temp -m "{\"v\":$((55 + RANDOM % 20)).$((RANDOM % 10))}" || break
                pub -t cars/car1/speed -m "$((RANDOM % 130))" || break
                if [ $((tick % 16)) -eq 0 ]; then
                    pub -t building/hvac/setpoint -m "{\"v\":$((18 + RANDOM % 5))}" || break
                fi
                sleep 0.5
            done
        ) >/dev/null 2>&1 &
        echo "$!" > tests/env/pusher.pid
    fi
    echo "test broker on :1889, profile 'testenv-local', telemetry pusher running (pid $(cat tests/env/pusher.pid))"
    if [ -z "{{only}}" ]; then
        just build
        # background jobs ignore SIGINT - kill the pusher ourselves when the app run ends (Ctrl+C included)
        trap 'kill "$(cat tests/env/pusher.pid 2>/dev/null)" 2>/dev/null || true; rm -f tests/env/pusher.pid' EXIT INT TERM
        MQTT_VIEWER_DB_PATH=tests/env/test.db ./builddir/mqtt-viewer || { ec=$?; [ "$ec" -eq 130 ] || exit "$ec"; }
    fi

# Stop the telemetry pusher + test broker and delete the test DB
testenv-stop:
    -[ -f tests/env/pusher.pid ] && kill "$(cat tests/env/pusher.pid)" 2>/dev/null
    -pkill -f "mosquitto -p 1889"
    rm -f tests/env/pusher.pid tests/env/test.db tests/env/test.db-shm tests/env/test.db-wal

# Show dependency versions vs upstream (wrapdb + pinned git/tarball wraps)
deps-check:
    @python3 scripts/deps.py check

# Bump dependencies. sqlite3 always auto (wrapdb); --auto also re-pins raylib/clay to latest upstream and rebuilds
[arg("auto", long="auto", value="--auto")]
deps-bump auto='':
    @python3 scripts/deps.py bump {{auto}}
