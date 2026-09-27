"""Checks a release directory as the gateway will: signature, then every image.

Usage:
    python verify_manifest.py <release_dir> [public_key.pem]

manifest.signed holds the base64 signature on its first line and the exact
manifest bytes after it; manifest.json, published for people, must equal them.
The signature is checked with the openssl command-line tool (bundled with Git
for Windows). The public key defaults to the one the firmware embeds.
"""

import base64
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

GATEWAY = Path(__file__).resolve().parents[2]


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    release = Path(sys.argv[1])
    public_key = Path(sys.argv[2]) if len(sys.argv) == 3 else GATEWAY / "gateway-signing.pub.pem"

    line, separator, manifest_bytes = (release / "manifest.signed").read_bytes().partition(b"\n")
    if not separator:
        sys.exit("manifest.signed has no signature line")
    if manifest_bytes != (release / "manifest.json").read_bytes():
        sys.exit("manifest.json differs from the manifest in manifest.signed")

    with tempfile.TemporaryDirectory() as directory:
        signature = Path(directory) / "manifest.sig"
        signature.write_bytes(base64.b64decode(line, validate=True))
        result = subprocess.run(
            ["openssl", "dgst", "-sha256", "-verify", str(public_key),
             "-signature", str(signature), str(release / "manifest.json")],
            capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"Signature check failed: {(result.stdout + result.stderr).strip()}")

    manifest = json.loads(manifest_bytes)
    failures = []
    for board, images in manifest["boards"].items():
        for kind, image in images.items():
            data = (release / image["file"]).read_bytes()
            if len(data) != image["size"] or hashlib.sha256(data).hexdigest() != image["sha256"]:
                failures.append(f"{board} {kind}: {image['file']}")
    if failures:
        sys.exit("Image does not match the manifest: " + ", ".join(failures))
    print(f"Release {manifest['version']} verified: signature and "
          f"{sum(len(images) for images in manifest['boards'].values())} images")


if __name__ == "__main__":
    main()
