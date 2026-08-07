# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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