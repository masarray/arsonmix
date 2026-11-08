"""Independently verify that a Windows release ZIP wraps the exact Inno EXE.

Only one flat entry, exact expected name, CRC, uncompressed size and SHA256.
Never trust the ZIP filename, GitHub asset digest, or source SHA alone.
"""
import argparse
import hashlib
import pathlib
import sys
import zipfile


def sha256_stream(stream):
    digest = hashlib.sha256()
    while True:
        block = stream.read(1024 * 1024)
        if not block:
            return digest.hexdigest()
        digest.update(block)


def verify(exe_path, zip_path):
    exe, archive_path = pathlib.Path(exe_path), pathlib.Path(zip_path)
    if not exe.is_file() or not archive_path.is_file():
        raise ValueError("Installer EXE and ZIP must exist")
    if not exe.name.endswith("-Windows-x64-Setup.exe"):
        raise ValueError("Only the exact Windows installer can be wrapped")
    if archive_path.name != exe.name[:-4] + ".zip":
        raise ValueError("Installer ZIP identity does not match EXE")
    if exe.stat().st_size < 2_097_152:
        raise ValueError("Unrealistically small Windows installer")
    with exe.open("rb") as stream:
        expected_sha = sha256_stream(stream)
    with zipfile.ZipFile(archive_path) as archive:
        entries = archive.infolist()
        if len(entries) != 1 or entries[0].is_dir() or entries[0].filename != exe.name:
            raise ValueError("ZIP must contain exactly one flat expected Inno EXE")
        if entries[0].file_size != exe.stat().st_size:
            raise ValueError("ZIP EXE byte size mismatch")
        if archive.testzip() is not None:
            raise ValueError("ZIP CRC verification failed")
        with archive.open(entries[0]) as stream:
            actual_sha = sha256_stream(stream)
    if actual_sha != expected_sha:
        raise ValueError("ZIP installer content differs from independently verified EXE")
    return expected_sha


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--zip", required=True)
    args = parser.parse_args()
    digest = verify(args.exe, args.zip)
    print("ARSONMIX_VERIFIED_SETUP_ZIP sha256=" + digest)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, zipfile.BadZipFile) as exc:
        sys.exit("Installer ZIP verification failed: " + str(exc))
