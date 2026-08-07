# MQTT Viewer

A simple and fast MQTT client for inspecting live broker traffic. Written in C23, rendered with [raylib](https://www.raylib.com/) + [Clay](https://github.com/nicbarker/clay). Message history is persisted to SQLite

## Features

- **Native C UI** - top performance, low memory usage, and no Electron overhead
- **Live topic tree** - realtime updates with global search, filtering, per-topic message counts and highlights
- **Payload inspector** - JSON pp, hex view, and a diff view against the previous message on a topic
- **Charts** - live plots of numeric payload values or JSON fields over time
- **Publish panel** - publish to any topic with QoS and retain control
- **Broker profiles** - save connection settings (host, creds, TLS, MQTT proto/transport, subscriptions)
- **Connection log** - timestamped connect/disconnect/error events in a panel
- **Live stats** - live message rate and throughput per subscription
- **Persistent history** - messages are stored in a local SQLite database across sessions (rotated to the newest 10k messages by default)
- **Export to CSV** - export a topic subtree's message history to CSV for offline analysis

## Motivation

I work in IoT, and MQTT is one of the two most used protocols in my workflow. I wanted a simple, fast, and native MQTT client to inspect live traffic and explore topics. There are several well-established MQTT clients, but none of them really fits my workflow:

- **[MQTT Explorer](https://github.com/thomasnordquist/MQTT-Explorer)** is the closest thing to what I wanted. But it is effectively unmaintained, and as an Electron/Node.js app, its performance is very slow for me
- **[MQTTX](https://mqttx.app/)** is actively maintained, but it is yet another Electron app, and its UX is built around connections and chat-style message streams rather than the topic-tree-first exploration workflow I actually use

Beyond filling that gap, the main motivation was the exercise itself: build the whole MQTT backend stack from scratch on top of libmosquitto, and put Clay through its paces as a C-native UI layout library (spoiler: it was such a pain...). So take it with the usual grain of salt: this is a personal project, not a polished product, and it is not intended for production use - although I have been using it every day for several weeks now

C23 was chosen to push my old C-ass to explore the new "features" gently added by the "beloved" committee (still no defer, yes). There is no backwards compatibility with older standards/compilers. The only thing I avoided is `constexpr`, because even now it is poorly supported by modern syntax-highlighting tools and IDEs

The code is formatted with [clang-format](https://clang.llvm.org/docs/ClangFormat.html); check the `.clang-format` file for the style used

## Platforms

- macOS
- Linux

## Build

Requires [Meson](https://mesonbuild.com/), Ninja, a C23 compiler, and `libmosquitto` + `libcjson` from your system. Raylib, SQLite, and Clay are fetched automatically as Meson subprojects pinned in [`subprojects/`](subprojects) (`just deps-check` shows pinned vs upstream versions)

The C23 compiler requirement is **GCC ≥ 15**, **Clang ≥ 19**, or **Apple Clang ≥ 17** (Xcode 16.3+)

```bash
# macOS
brew install meson ninja mosquitto cjson

# Debian/Ubuntu
sudo apt install meson ninja-build libmosquitto-dev libcjson-dev
```

It is highly recommended to install [just](https://github.com/casey/just) as a dev dependency - the `justfile` wraps all common recipes (a modern makefile alternative), such as:

```bash
just build                # debugoptimized build (default); also --debug / --release
just run                  # build + run; also --debug / --release
just test                 # run all unit tests
just dbgr                 # run the debug build under debugger (requires lldb/gdb installed)
just docs                 # generate Doxygen API docs (requires doxygen)
just package              # build a release package; --target macos (.dmg) / linux (.deb)
just testenv              # local test broker + sample traffic + app; --only-env to skip the app
just testenv-stop         # stop the test broker/publisher and delete the test db
just clean                # remove all build artefacts and tear down the test environment
```

So e.g. to build the release binary and run it, you can just do:

```bash
just run
```

## Environment Variables

- `MQTT_VIEWER_DB_PATH` - overrides the path to the SQLite db. Default: `~/Library/Application Support/mqtt-viewer/mqtt-viewer.db` on macOS, `$XDG_DATA_HOME/mqtt-viewer/mqtt-viewer.db` (falling back to `~/.local/share/...`) on Linux
- `MQTT_VIEWER_CSV_EXPORT_PATH` - overrides the directory CSV exports are written to. Default: `~/Downloads` (falling back to `$XDG_DOWNLOAD_DIR`, then `$HOME`)

## Packages

Prebuilt packages (`.dmg` for macOS arm64, `.deb` for amd64/arm64) are attached to every [GitHub Release](../../releases) - built and published automatically from version tags

Build them manually with `just package --target macos` (.dmg) or `just package --target linux` (.deb); only Debian-based distros are supported for now

Packaging must run on the target OS - there is no cross-compilation so far, and the `.deb` step needs `dpkg-deb` while the `.dmg` step needs `dylibbundler`

The packaged binary statically links raylib and SQLite; `libmosquitto` and `libcjson` remain dynamic and must be installed on the target system. The `.deb` package declares them as dependencies, but the `.dmg` package does not (macOS has no s-w package manager)

## License

The project is licensed under [Apache License 2.0](LICENSE) - Copyright © 2026 Nikita Smirnov

Built with (fetched as pinned Meson subprojects, statically linked into release binaries):

- [Clay](https://github.com/nicbarker/clay) - zlib/libpng license, Copyright © 2024 Nic Barker
- [raylib](https://www.raylib.com/) - zlib/libpng license, Copyright © 2013-2026 Ramon Santamaria
- [SQLite](https://sqlite.org/) - public domain

Bundled 3rd-party components keep their own licenses:

- Fonts [Inter](https://github.com/rsms/inter) and [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) - [SIL Open Font License 1.1](resources/fonts/) (embedded in the binary)

## Author

[Nikita Smirnov](https://github.com/gehirndienst)

