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
    export MQTT_VIEWER_DB=mqtt_viewer.db
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

# Remove all build artefacts
clean:
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
