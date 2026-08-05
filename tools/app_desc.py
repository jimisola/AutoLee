#!/usr/bin/env python3
"""Read the `esp_app_desc_t` header out of a built ESP-IDF app image.

Why this exists: the firmware version is derived from git at *configure* time
(`PROJECT_VER` <- `git describe --always --tags --dirty`) and baked into the
binary. Nothing in the source states it, so the only way to check that a
release artifact carries the version it claims is to read it back out of the
image — which is exactly what `.github/workflows/release.yml` does before it
publishes anything.

CONTRIBUTING.md's "Versioning & releases" documents two ways that string can be
wrong without anything looking wrong: `PROJECT_VER` is cached at CMake
configure time rather than recomputed per build, and `git describe` picks the
nearest tag by commit-graph distance rather than the highest semver. A
`-dirty` suffix from a modified tree is a third. This turns all three from a
silently mis-stamped release into a failed one.

Layout (esp_app_format.h): the descriptor sits immediately after the 24-byte
image header plus the 8-byte header of the first segment, i.e. at offset 0x20.

    0x20  uint32  magic_word      (0xABCD5432)
    0x24  uint32  secure_version
    0x28  uint32  reserv1[2]
    0x30  char    version[32]
    0x50  char    project_name[32]
    0x70  char    time[16]
    0x80  char    date[16]
    0x90  char    idf_ver[32]

Usage:
    python3 tools/app_desc.py build/autolee.bin            # print all fields
    python3 tools/app_desc.py build/autolee.bin --field version
"""

from __future__ import annotations

import argparse
import struct
import sys

DESC_OFFSET = 0x20
DESC_MAGIC = 0xABCD5432

# name -> (offset relative to DESC_OFFSET, size)
FIELDS = {
    "version": (0x10, 32),
    "project_name": (0x30, 32),
    "time": (0x50, 16),
    "date": (0x60, 16),
    "idf_ver": (0x70, 32),
}


def _find_desc(blob: bytes, scan: bool) -> int:
    """Return the absolute offset of the descriptor, or raise."""
    if not scan:
        if len(blob) >= DESC_OFFSET + 4:
            (magic,) = struct.unpack_from("<I", blob, DESC_OFFSET)
            if magic == DESC_MAGIC:
                return DESC_OFFSET
        raise SystemExit(
            f"no esp_app_desc_t at {DESC_OFFSET:#x} (expected magic {DESC_MAGIC:#010x}). "
            "A merged full-flash image starts at the bootloader, not the app — pass "
            "--scan to find the first app image inside it."
        )

    # A merged image begins with the bootloader and the partition table, so the
    # app's descriptor is at whatever offset the partition table happens to
    # place ota_0 at (0x20000 with the current partitions.csv, but that is
    # derived, not declared — the CSV leaves the offset blank). Scanning for the
    # magic keeps this correct if the table is ever re-laid-out.
    needle = struct.pack("<I", DESC_MAGIC)
    at = blob.find(needle)
    if at < 0:
        raise SystemExit(f"no esp_app_desc_t magic ({DESC_MAGIC:#010x}) anywhere in the image")
    return at


def read_app_desc(path: str, scan: bool = False) -> dict[str, str]:
    with open(path, "rb") as fh:
        # Whole file when scanning; a merged 4 MB image is small enough to slurp.
        blob = fh.read() if scan else fh.read(DESC_OFFSET + 0x100)

    try:
        base = _find_desc(blob, scan)
    except SystemExit as exc:
        raise SystemExit(f"{path}: {exc}") from None

    if len(blob) < base + 0x90:
        raise SystemExit(f"{path}: truncated esp_app_desc_t at {base:#x}")

    out = {}
    for name, (rel, size) in FIELDS.items():
        raw = blob[base + rel : base + rel + size]
        out[name] = raw.split(b"\0", 1)[0].decode("utf-8", "replace")
    return out


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Read the esp_app_desc_t header out of a built ESP-IDF app image."
    )
    parser.add_argument("image", help="path to the app binary (e.g. build/autolee.bin)")
    parser.add_argument(
        "--field",
        choices=sorted(FIELDS),
        help="print just this field, bare, with no label (for shell capture)",
    )
    parser.add_argument(
        "--scan",
        action="store_true",
        help="search the whole file for the descriptor instead of reading it at "
        "0x20 — use for a merged full-flash image, where the app is not at the start",
    )
    args = parser.parse_args()

    desc = read_app_desc(args.image, scan=args.scan)
    if args.field:
        print(desc[args.field])
    else:
        for name in FIELDS:
            print(f"{name:14} {desc[name]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
