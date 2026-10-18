#!/usr/bin/env python3
"""Collect redistributable sample resources that are actually present in EA's source release.

This does NOT extract commercial game data from an installed copy of Generals/Zero Hour.
It only copies source-release resources into a clearly labelled sample directory.
"""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "upstream" / "ea-generals-zero-hour"
OUT = ROOT / "assets" / "ea-source-samples"

ALLOW = {".bmp", ".ico", ".cur", ".ani", ".rc", ".dlg", ".txt", ".ini"}
DENY_PARTS = {"thirdparty", "libraries", "library", "sdk", "run"}


def allowed(path: Path) -> bool:
    rel = path.relative_to(UPSTREAM)
    lowered = {part.lower() for part in rel.parts}
    return path.suffix.lower() in ALLOW and not (lowered & DENY_PARTS)


def main() -> int:
    if not UPSTREAM.exists():
        raise SystemExit("EA upstream tree is missing. Run the import workflow first.")
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    copied = 0
    manifest = []
    for source in UPSTREAM.rglob("*"):
        if not source.is_file() or not allowed(source):
            continue
        rel = source.relative_to(UPSTREAM)
        target = OUT / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        manifest.append(str(rel).replace("\\", "/"))
        copied += 1

    (OUT / "MANIFEST.txt").write_text(
        "EA source-release sample resources\n"
        "Generated from files already present in electronicarts/CnC_Generals_Zero_Hour.\n"
        "No assets were extracted from an installed commercial copy.\n\n" +
        "\n".join(manifest) + "\n",
        encoding="utf-8",
    )
    print(f"Copied {copied} source-release resources to {OUT}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
