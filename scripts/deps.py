#!/usr/bin/env python3

import hashlib
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

RAYLIB_WRAP = Path("subprojects/raylib.wrap")
RAYLIB_OVERLAY = Path("subprojects/packagefiles/raylib/meson.build")
CLAY_WRAP = Path("subprojects/clay.wrap")

RAYLIB_REPO = "https://github.com/raysan5/raylib"
CLAY_REPO = "https://github.com/nicbarker/clay.git"


def run(*cmd: str) -> str:
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def wrap_field(path: Path, key: str) -> str:
    match = re.search(rf"(?m)^{key} = (.+)$", path.read_text())
    if not match:
        sys.exit(f"{path}: field '{key}' not found")
    return match.group(1).strip()


def set_wrap_field(path: Path, key: str, value: str) -> None:
    path.write_text(re.sub(rf"(?m)^{key} = .*$", f"{key} = {value}", path.read_text()))


def raylib_pinned() -> str:
    return wrap_field(RAYLIB_WRAP, "directory").removeprefix("raylib-")


def raylib_latest_tag() -> str:
    tags = re.findall(
        r"refs/tags/(\d+\.\d+(?:\.\d+)?)$",
        run("git", "ls-remote", "--tags", RAYLIB_REPO),
        re.MULTILINE,
    )
    return max(tags, key=lambda tag: [int(part) for part in tag.split(".")])


def clay_pinned() -> str:
    return wrap_field(CLAY_WRAP, "revision")


def clay_upstream_head() -> str:
    return run("git", "ls-remote", CLAY_REPO, "HEAD").split()[0]


def sqlite_pinned() -> str:
    return wrap_field(Path("subprojects/sqlite3.wrap"), "wrapdb_version")


def check() -> None:
    print(f"sqlite3  pinned {sqlite_pinned()} (wrapdb; 'deps.py bump' updates it)")
    print(f"raylib   pinned {raylib_pinned()}, latest tag {raylib_latest_tag()}")
    head = clay_upstream_head()
    clay = clay_pinned()
    state = "== upstream HEAD" if clay == head else f"upstream HEAD is {head[:10]}"
    print(f"clay     pinned {clay[:10]}, {state}")


def bump_sqlite() -> None:
    subprocess.run(["meson", "wrap", "update", "sqlite3"], check=True)


def bump_raylib() -> bool:
    pinned, latest = raylib_pinned(), raylib_latest_tag()
    if pinned == latest:
        print(f"raylib: {pinned} is latest")
        return False
    print(f"raylib: {pinned} -> {latest}, downloading tarball for its checksum...")
    url = f"{RAYLIB_REPO}/archive/refs/tags/{latest}.tar.gz"
    with urllib.request.urlopen(url) as response:
        checksum = hashlib.sha256(response.read()).hexdigest()
    set_wrap_field(RAYLIB_WRAP, "directory", f"raylib-{latest}")
    set_wrap_field(RAYLIB_WRAP, "source_url", url)
    set_wrap_field(RAYLIB_WRAP, "source_filename", f"raylib-{latest}.tar.gz")
    set_wrap_field(RAYLIB_WRAP, "source_hash", checksum)
    RAYLIB_OVERLAY.write_text(
        re.sub(r"version: '[\d.]+'", f"version: '{latest}'", RAYLIB_OVERLAY.read_text())
    )
    print(
        "raylib: re-pinned (if the build fails, check the source list in the overlay meson.build)"
    )
    return True


def bump_clay() -> bool:
    pinned, head = clay_pinned(), clay_upstream_head()
    if pinned == head:
        print("clay: pinned to upstream HEAD")
        return False
    print(f"clay: {pinned[:10]} -> {head[:10]}")
    set_wrap_field(CLAY_WRAP, "revision", head)
    return True


def rebuild_and_test() -> None:
    for stale in Path("subprojects").glob("raylib-*"):
        subprocess.run(["rm", "-rf", str(stale)], check=True)
    subprocess.run(["rm", "-rf", "subprojects/clay"], check=True)
    subprocess.run(["just", "build"], check=True)
    subprocess.run(["just", "test"], check=True)
    print("deps bumped and verified - review 'git diff subprojects/' and commit")


def bump(auto: bool) -> None:
    bump_sqlite()
    if not auto:
        print()
        print("re-pin raylib/clay automatically with: just deps-bump --auto")
        print("or by hand: edit subprojects/raylib.wrap + subprojects/clay.wrap,")
        print(
            "then rm -rf subprojects/raylib-* subprojects/clay && just build && just test"
        )
        return
    if bump_raylib() | bump_clay():
        rebuild_and_test()
    else:
        print("nothing to bump besides sqlite3")


def main() -> None:
    args = sys.argv[1:]
    if args[:1] == ["check"]:
        check()
    elif args[:1] == ["bump"]:
        bump(auto="--auto" in args[1:])
    else:
        sys.exit(__doc__.strip())


if __name__ == "__main__":
    main()
