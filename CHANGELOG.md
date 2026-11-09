# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-11-09

### Added

- Retained-message deletion from the inspector's button or the topic context menu's (whole subtree). Publishes an empty retained message; the R badge clears only when the broker echoes it back
- About dialog with version, build date, author and a manual "Check for updates" against the latest GitHub release with curl
- DMG-application drag-and-drop to /Applications and artwork for the app icon and DMG background
- Charts show the topic name above each plot

### Changed

- Chart grid follows the data: the Y range snaps to round values with one labelled grid line per step
- New-message highlights in the topic tree use much darker purple shades to keep busy topics readable
- Status bar shows only global stats; per-subscription counters removed
- Raylib updated to 6.2; the pinned wrap is now always used instead of a system raylib
- Improved meson.build scripts

### Fixed

- UI no longer freezes for up to a second under high message rates: history is written to the database one small batch per frame, and old rows are trimmed by rowid every second instead of with a full-table sort every five seconds
- Inspector no longer flickers on frequent updates: the latest-value block keeps a constant height
- Fonts are re-baked when the display scale changes (sleep/wake, moving between displays)
- Charts are closed on disconnect instead of lingering with stale data
- Topic paths preserve empty levels (such as `a//b` and `/a/`); unsupported names are rejected instead of silently truncated before publishing
- Clicking a parent topic's name without messages now toggles expansion and collapse instead of only expanding it

## [1.0.5] - 2026-09-30

### Added

- Caret-aware text editing in the profile dialog, publish panel and topic/message filter
- Scrollable connection log panel
- WebSocket path field in the broker profile
- The connection log reports a subscription the broker denies (MQTT 3.1.1 `0x80` or any MQTT 5 failure reason code) as an error

### Changed

- Fonts are rasterized at the display's physical pixel size and text is drawn on whole pixels
- The "messages dropped" warning in the connection log is throttled to one line per 5 seconds
- `build-dmg.sh` uses `diskutil image create` instead of the deprecated `hdiutil create`

### Fixed

- Text looked pixelated on 1x displays
- Typed non-ASCII characters in text fields were garbled
- A failed subscribe request was silently ignored
- JSON detection: `strtod` stopping at the first unparseable byte, and whitespace is now trimmed before deciding whether a payload is JSON
- Build failure on clang 23 (`-Wunused-but-set-global`)

## [1.0.4] - 2026-09-16

### Added

- CBOR tab in the topic inspector: strict RFC 8949 decoding
- The topic filter keeps a node visible when any of its descendants match, so matches inside collapsed subtrees stay reachable
- The connection log warns when incoming messages are dropped because the queue is full or an allocation failed with a running count
- @file blocks for the Doxygen API docs

### Changed

- Removed the bundled cJSON dependency, replaced by the selfmade JSON line formatter
- Message buffer and connection log carry a generation counter so the history and log views rebuild only when the contents actually changed
- The hex tab shows the wire bytes of the latest message; it used to hexdump the sanitized text preview so binary payloads were misrepresented
- Charts keep the most deviating sample within each 50 ms window instead of decimating
- Topic nodes allocate their payload snapshot lazily cutting per-topic memory on trees with many silent nodes
- Shared `model/util` and `ui/ui_util` helpers

### Fixed

- Clear History now drops the topic's messages from the in-memory history and the database not just the tree node's counters
- Sorted recipes in the justfile

## [1.0.3] - 2026-09-02

### Added

- Full-text search across stored messages: the tree's filter bar toggles between `Topics` and `Messages`, backed by an SQLite FTS5 index over topic and payload
- New-message highlight in the topic tree
- Connect through an SSH jump host: per-profile tunnel settings in the connection dialog
- Selecting and copying in the connection log
- Select-all (`Ctrl+A`) in text input fields

### Fixed

- The chart panel's `x` button did nothing once the topic inspector was closed
- Topic-tree row highlights (selection and new-message) now run flush to the inspector edge instead of stopping short of it

## [1.0.2] - 2026-08-07

### Added

- Export a topic subtree's message history to CSV via the CSV button in the topic inspector. `MQTT_VIEWER_CSV_EXPORT_PATH` env var to override the CSV export directory

### Fixed

- UI no longer freezes when connecting to an unreachable broker (e.g. behind a VPN that drops traffic) - the connect runs on the MQTT network thread now, "Connecting..." shows immediately and failures land in the connection log

### Changed

- Full payload persistence: message payloads survive restart losslessly (previously only 512-byte previews)
- raylib upgraded to 6.0; SQLite to 3.53.4
- Clay is now a pinned Meson subproject instead of a vendored header; all three dependencies update via `just deps-check` / `just deps-bump`

## [1.0.1] - 2026-08-05

### Added

- Message counter badges for parent topics in the topic tree
- Payload persistence across sessions (messages survive app restart)
- Paste from clipboard into all text input fields
- Packaged releases: built and published automatically from version tags via GitHub Actions.

### Changed

- Shrunk the payload preview in the topic tree for a denser layout.

## [1.0.0] - 2026-07-12

### Added

- Initial release
