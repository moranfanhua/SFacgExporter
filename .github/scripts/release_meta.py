"""Resolve a checked-out version tag without interpolating user input into shell code."""
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main():
    tag = os.environ["RELEASE_TAG"]
    match = re.fullmatch(r"v(\d+\.\d+\.\d+)(-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", tag)
    if not match:
        raise SystemExit("Use a version tag such as v0.10.0 or v0.10.0-beta.1")
    version = match.group(1) + (match.group(2) or "")
    commit = git("rev-parse", f"refs/tags/{tag}^{{commit}}")
    if commit != git("rev-parse", "HEAD"):
        raise SystemExit("The checkout must match the existing release tag")
    project = "src/SFEAvalonia/SFEAvalonia.csproj"
    configured = ET.parse(ROOT / project).findtext(".//Version")
    if configured != match.group(1):
        raise SystemExit(f"{project}: Version must match {match.group(1)} before tagging")
    values = {"tag": tag, "version": version, "commit": commit, "prerelease": str(bool(match.group(2))).lower()}
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        for key, value in values.items():
            output.write(f"{key}={value}\n")


if __name__ == "__main__":
    main()
