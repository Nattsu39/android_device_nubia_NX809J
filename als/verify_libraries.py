#!/usr/bin/env python3
"""Check source or installed sender libraries against the reviewed dependency closure."""

import argparse
import hashlib
import json
from pathlib import Path


def verify(directory: Path, manifest: Path) -> None:
    data = json.loads(manifest.read_text(encoding="utf-8"))
    available = {row["name"] for row in data["libraries"]} | set(data["shared"])
    for row in data["libraries"]:
        path = directory / row["name"]
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing regular sender library: {path}")
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != row["sha256"]:
            raise ValueError(f"Sender library differs from pinned input: {path}")
        missing = set(row["needed"]) - available
        if missing:
            raise ValueError(f"Unresolved private dependencies of {path.name}: {sorted(missing)}")
    print(f"PASS: {len(data['libraries'])} pinned libraries; complete dependency closure")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    verify(args.directory, Path(__file__).with_name("vendor-libraries.json"))


if __name__ == "__main__":
    main()
