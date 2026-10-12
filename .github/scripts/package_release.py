"""Package only distributable files; never include sessions, caches, logs or test output."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "artifacts/release"


def main():
    version = os.environ["RELEASE_VERSION"]
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", version):
        raise SystemExit("Invalid release version")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    mode = sys.argv[1]
    if mode == "windows":
        published = ROOT / "artifacts/win-x64"
        package = OUTPUT / f"SFacgExporter-{version}-windows-x64.zip"
        entries = [(published / "菠萝包小说导出.exe", "菠萝包小说导出.exe"),
                   (published / "SFECore.dll", "SFECore.dll"),
                   (published / "resource/cipher-table.json", "resource/cipher-table.json"),
                   (published / "resource/cacert.pem", "resource/cacert.pem"),
                   (ROOT / "README.md", "README.md"), (ROOT / "LICENSE", "LICENSE")]
        with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for source, name in entries:
                archive.write(source, name)
    elif mode == "android":
        packages = list((ROOT / "artifacts/android").glob("*-Signed.apk"))
        if len(packages) != 1:
            raise SystemExit("Expected one signed Android APK")
        shutil.copyfile(packages[0], OUTPUT / f"SFacgExporter-{version}-android-arm64.apk")
    elif mode == "source":
        expected = [OUTPUT / f"SFacgExporter-{version}-windows-x64.zip", OUTPUT / f"SFacgExporter-{version}-android-arm64.apk"]
        if not all(path.is_file() for path in expected):
            raise SystemExit("Both platform builds are required before publishing")
        source = OUTPUT / f"SFacgExporter-{version}-source.zip"
        subprocess.run(["git", "archive", "--format=zip", f"--prefix=SFacgExporter-{version}/", f"--output={source}", "HEAD"], cwd=ROOT, check=True)
        files = sorted([*expected, source], key=lambda path: path.name)
        with (OUTPUT / "SHA256SUMS.txt").open("w", encoding="utf-8", newline="\n") as checksums:
            for path in files:
                with path.open("rb") as stream:
                    digest = hashlib.file_digest(stream, "sha256").hexdigest()
                checksums.write(f"{digest}  {path.name}\n")
    else:
        raise SystemExit("Expected windows, android or source")


if __name__ == "__main__":
    main()
