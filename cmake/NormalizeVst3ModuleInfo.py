#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


def normalise_module_info(path: Path, check_only: bool) -> None:
    original = path.read_text(encoding="utf-8")
    strict_json = re.sub(r",(\s*[\]}])", r"\1", original)
    parsed = json.loads(strict_json)

    if not check_only:
        path.write_text(json.dumps(parsed, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Normalise JUCE-generated VST3 moduleinfo.json to strict JSON."
    )
    parser.add_argument("--check", action="store_true", help="validate without rewriting")
    parser.add_argument("module_info", type=Path)
    args = parser.parse_args()

    normalise_module_info(args.module_info, args.check)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
