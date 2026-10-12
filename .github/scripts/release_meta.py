"""Resolve a checked-out version tag without interpolating user input into shell code."""
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main():
    tag = os.environ["RELEASE_TAG"]
    match = re.fullmatch(r"v(\d+\.\d+\.\d+)(-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", tag)
    if not match:
        raise SystemExit("Use a version tag such as v1.0.0 or v1.0.0-beta.1")
    version = match.group(1) + (match.group(2) or "")
    commit = git("rev-parse", f"refs/tags/{tag}^{{commit}}")
    if commit != git("rev-parse", "HEAD"):
        raise SystemExit("The checkout must match the existing release tag")
    on_master = subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "refs/remotes/origin/master"],
        cwd=ROOT, check=False,
    )
    if on_master.returncode == 1:
        raise SystemExit("Release tags must point to a commit on master; merge dev before tagging")
    on_master.check_returncode()
    major, minor, patch = map(int, match.group(1).split("."))
    android_version_code = major * 1_000_000 + minor * 1_000 + patch
    if minor >= 1000 or patch >= 1000 or not 0 < android_version_code <= 2_100_000_000:
        raise SystemExit("Version cannot be represented as an Android version code")
    values = {
        "tag": tag,
        "version": version,
        "commit": commit,
        "prerelease": str(bool(match.group(2))).lower(),
        "android_version_code": str(android_version_code),
    }
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        for key, value in values.items():
            output.write(f"{key}={value}\n")


if __name__ == "__main__":
    main()
