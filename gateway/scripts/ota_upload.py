Import("env")

import re
from pathlib import Path


secrets_path = Path(env.subst("$PROJECT_DIR")) / "include" / "LocalSecrets.h"
if not secrets_path.is_file():
    raise RuntimeError(
        "OTA upload requires include/LocalSecrets.h; copy it from "
        "include/LocalSecrets.example.h first."
    )

contents = secrets_path.read_text(encoding="utf-8")
match = re.search(
    r'^\s*#define\s+GATEWAY_OTA_PASSWORD\s+"([^"]+)"',
    contents,
    flags=re.MULTILINE,
)
if match is None or not match.group(1):
    raise RuntimeError("GATEWAY_OTA_PASSWORD is missing or empty in LocalSecrets.h.")

env.Append(
    UPLOAD_FLAGS=[
        "--host_port=3233",
        "--auth=" + match.group(1),
    ]
)
