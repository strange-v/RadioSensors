"""Collects built gateway images into a release directory with manifest.json.

Usage:
    python make_manifest.py <version> <output_dir>

Run from anywhere after `npm run build` in ui/ and, for every board,
`pio run -e <env>` and `pio run -e <env> -t buildfs`. The manifest is signed
separately (see README, Releases); signing the exact file bytes avoids any
JSON canonicalization.
"""

import hashlib
import json
import re
import shutil
import sys
import zlib
from pathlib import Path

GATEWAY = Path(__file__).resolve().parents[2]
# releaseId from include/BoardProfile.h -> PlatformIO environment.
BOARDS = {
    "wt32-eth01": "gateway_wt32_eth01",
    "waveshare-s3-eth": "gateway_waveshare_s3_eth",
}
MANIFEST_FORMAT = 2
# gzip with a 4 KB deflate window: the gateway inflates the Web UI image with a
# 4 KB buffer (kUiWindowSize in include/ReleaseManifest.h).
UI_WINDOW_BITS = 12
VERSION_PATTERN = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")


def firmware_version():
    source = (GATEWAY / "include" / "FirmwareVersion.h").read_text(encoding="utf-8")
    match = re.search(r'version\[\]\s*=\s*"([^"]+)"', source)
    if match is None:
        sys.exit("FirmwareVersion.h has no version")
    return match.group(1)


def describe(source, target):
    shutil.copyfile(source, target)
    data = target.read_bytes()
    return {
        "file": target.name,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def describe_compressed(source, target):
    image = source.read_bytes()
    compressor = zlib.compressobj(9, zlib.DEFLATED, 16 + UI_WINDOW_BITS)
    target.write_bytes(compressor.compress(image) + compressor.flush())
    return {
        "file": target.name,
        "size": target.stat().st_size,
        "image_size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
    }


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    version, output = sys.argv[1], Path(sys.argv[2])
    match = VERSION_PATTERN.match(version)
    if match is None:
        sys.exit(f"Release version must be major.minor.patch: {version}")
    if version != firmware_version():
        sys.exit(f"Release {version} does not match FirmwareVersion.h {firmware_version()}")

    ui_manifest = json.loads((GATEWAY / "data" / "ui-manifest.json").read_text(encoding="utf-8"))
    if ui_manifest["ui_version"] != version:
        sys.exit(f"Web UI in data/ is {ui_manifest['ui_version']}, release is {version}")

    output.mkdir(parents=True, exist_ok=True)
    boards = {}
    for board, environment in BOARDS.items():
        build = GATEWAY / ".pio" / "build" / environment
        boards[board] = {
            "firmware": describe(
                build / "firmware.bin", output / f"gateway-{board}-firmware.bin"),
            "ui": describe_compressed(
                build / "littlefs.bin", output / f"gateway-{board}-ui.bin.gz"),
        }

    manifest = {"format": MANIFEST_FORMAT, "version": version, "boards": boards}
    (output / "manifest.json").write_text(
        json.dumps(manifest, separators=(",", ":")) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
