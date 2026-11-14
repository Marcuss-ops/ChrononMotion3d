#!/usr/bin/env python3
"""Upload the compositing showcase into one Drive subfolder.

The first upload creates `cm3d_compositing_20261110` under the shared folder and
prints the child folder id. Subsequent uploads target that id directly, so the
Drive folder is not duplicated for every file.
"""

from __future__ import annotations

import sys as _velox_sys
from pathlib import Path as _VeloxPath
_velox_sys.path.insert(0, str(_VeloxPath(__file__).resolve().parents[2] / "tools"))
from stack_config import credentials_file as _credentials_file  # noqa: E402
from stack_config import overlay_root as _overlay_root  # noqa: E402
from stack_config import token_file as _token_file  # noqa: E402

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
UPLOADER = REPO / "RenderingGen/bin/drive-upload"
CREDENTIALS = _credentials_file()
TOKEN = _token_file()

ROOT_FOLDER = _overlay_root()
SUBFOLDER = "cm3d_compositing_20261110"
SHOWCASE = REPO / "RenderingGen/Upload/cm3d_compositing_20261110"
README = "README.txt"
FILES = [
    "01_depth_sort_crossing_1080p.mp4",
    "02_stack_vs_depth_1080p.mp4",
    "03_text_behind_object_1080p.mp4",
    "contact_sheet.png",
    "showcase.json",
    "videos_manifest.json",
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def field(output: str, name: str) -> str:
    match = re.search(rf"(?:^|\s){re.escape(name)}=(\S+)", output)
    return match.group(1) if match else ""


def upload(path: Path, folder: str, subfolder: str | None = None) -> dict[str, str]:
    digest = sha256(path)
    command = [
        str(UPLOADER),
        "-credentials", str(CREDENTIALS),
        "-token", str(TOKEN),
        "-folder", folder,
        "-file", str(path),
        "-name", path.name,
        "-sha256", digest,
    ]
    if subfolder:
        command += ["-subfolder", subfolder]

    result = subprocess.run(command, capture_output=True, text=True)
    output = (result.stdout + "\n" + result.stderr).strip()
    print(output, flush=True)
    if result.returncode != 0 or "DRIVE_UPLOAD_PASS" not in output:
        raise SystemExit(f"upload failed for {path.name} (rc={result.returncode})")

    return {
        "file": path.name,
        "bytes": str(path.stat().st_size),
        "sha256": digest,
        "id": field(output, "id"),
        "link": field(output, "link"),
        "parent": field(output, "parent"),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--parent", default="", help="existing Drive child folder id")
    parser.add_argument("--readme-only", action="store_true", help="only create the folder and upload README")
    args = parser.parse_args()

    if not CREDENTIALS.exists() or not TOKEN.exists():
        raise SystemExit(f"missing OAuth files: {CREDENTIALS} / {TOKEN}")

    if args.parent:
        parent = args.parent
        folder_link = ""
    else:
        info = upload(SHOWCASE / README, ROOT_FOLDER, SUBFOLDER)
        parent = info["parent"]
        folder_link = info["link"]
        print(f"parent={parent}", flush=True)
        (SHOWCASE / "drive_folder.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
        if args.readme_only:
            return 0
        if not parent:
            raise SystemExit("uploader did not return parent=; cannot place the remaining files")

    results = []
    for name in FILES:
        path = SHOWCASE / name
        if not path.exists():
            raise SystemExit(f"missing local file: {path}")
        results.append(upload(path, parent))

    manifest = {
        "schema": "chrononmotion.compositing-showcase.drive.v1",
        "drive_folder_id": ROOT_FOLDER,
        "drive_subfolder": SUBFOLDER,
        "drive_subfolder_id": parent,
        "drive_subfolder_link": folder_link,
        "files": results,
    }
    local_manifest = SHOWCASE / "upload_manifest.json"
    local_manifest.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    upload(local_manifest, parent)

    print("\n--- DRIVE LINKS ---")
    if folder_link:
        print(f"folder: {folder_link}")
    for item in results:
        print(f"{item['file']}: {item['link']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
