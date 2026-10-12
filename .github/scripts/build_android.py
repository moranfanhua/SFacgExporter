"""Build the native core and sign an Android APK with a persistent repository key."""
import base64
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NDK_VERSION = "29.0.14206865"


def run(*args, env=None, input=None):
    subprocess.run(args, cwd=ROOT, env=env, input=input, text=True, check=True)


def main():
    required = ["ANDROID_KEYSTORE_BASE64", "ANDROID_KEY_ALIAS", "ANDROID_KEY_PASSWORD", "ANDROID_STORE_PASSWORD"]
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise SystemExit("Configure repository signing secrets: " + ", ".join(missing))
    sdk = Path(os.environ.get("ANDROID_HOME") or os.environ["ANDROID_SDK_ROOT"])
    manager = shutil.which("sdkmanager")
    if manager is None:
        manager = str(sdk / "cmdline-tools/latest/bin/sdkmanager")
    run(manager, f"--sdk_root={sdk}", "--licenses", input="y\n" * 1000)
    run(manager, f"--sdk_root={sdk}", "platforms;android-36", "build-tools;36.0.0", f"ndk;{NDK_VERSION}", input="y\n" * 1000)
    env = dict(os.environ)
    env["ANDROID_NDK_HOME"] = str(sdk / "ndk" / NDK_VERSION)
    run("cmake", "--preset", "android-arm64", env=env)
    run("cmake", "--build", "--preset", "android-arm64-Release", env=env)
    # Keep signing material outside artifacts. Passwords are read by MSBuild from environment variables.
    with tempfile.TemporaryDirectory(prefix="sfexporter-signing-") as temp:
        key = Path(temp) / "release.keystore"
        key.write_bytes(base64.b64decode(os.environ["ANDROID_KEYSTORE_BASE64"], validate=True))
        key.chmod(0o600)
        run("dotnet", "publish", "src/SFEAvalonia/SFEAvalonia.csproj", "-p:SFEPlatform=Android", "-c", "Release", "-o", "artifacts/android",
            f"-p:AndroidSdkDirectory={sdk}", f"-p:JavaSdkDirectory={os.environ['JAVA_HOME']}",
            "-p:AndroidKeyStore=true", f"-p:AndroidSigningKeyStore={key}",
            f"-p:AndroidSigningKeyAlias={os.environ['ANDROID_KEY_ALIAS']}",
            "-p:AndroidSigningKeyPass=env:ANDROID_KEY_PASSWORD", "-p:AndroidSigningStorePass=env:ANDROID_STORE_PASSWORD", env=env)


if __name__ == "__main__":
    main()
