"""Regenerate the wholly authored four-pixel-wide skin demonstration payload."""

import hashlib
import json
from pathlib import Path
import struct


def main():
    root = Path(__file__).resolve().parent
    pixels = b"".join(
        struct.pack("<H", 0xF81F if (x + y) % 2 else 0x07E0)
        for y in range(4)
        for x in range(4)
    )
    identity = 14695981039346656037
    for byte in pixels:
        identity = ((identity ^ byte) * 1099511628211) & ((1 << 64) - 1)
    payload = (
        b"BWTEX1\0\0"
        + struct.pack("<III", 1, 1, 0)
        + b"ccdemo_checker".ljust(32, b"\0")
        + struct.pack("<IIQ", 4, 4, identity)
        + pixels
    )
    (root / "checker.bwtex").write_bytes(payload)
    path = root / "pack.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    manifest["files"]["checker.bwtex"] = {
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
