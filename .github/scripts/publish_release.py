"""Upload complete build artifacts to a draft, then publish it."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def gh(*args):
    subprocess.run(["gh", *args], cwd=ROOT, check=True)


def main():
    tag = os.environ["RELEASE_TAG"]
    version = tag.removeprefix("v")
    assets = [ROOT / "artifacts/release" / name for name in [
        f"SFacgExporter-{version}-windows-x64.zip", f"SFacgExporter-{version}-android-arm64.apk",
        f"SFacgExporter-{version}-source.zip", "SHA256SUMS.txt"]]
    if not all(path.is_file() for path in assets):
        raise SystemExit("Release assets are incomplete")
    existing = subprocess.run(["gh", "release", "view", tag, "--json", "isDraft"], cwd=ROOT, capture_output=True, text=True)
    if existing.returncode == 0:
        if not json.loads(existing.stdout)["isDraft"]:
            raise SystemExit("This release is already published; use a new version tag")
    else:
        args = ["release", "create", tag, "--verify-tag", "--draft", "--title", f"SFacgExporter {tag}", "--generate-notes"]
        if os.environ["RELEASE_PRERELEASE"] == "true":
            args.append("--prerelease")
        gh(*args)
    gh("release", "upload", tag, *(str(path) for path in assets), "--clobber")
    gh("release", "edit", tag, "--draft=false")


if __name__ == "__main__":
    main()
