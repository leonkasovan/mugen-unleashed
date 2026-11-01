#!/usr/bin/env python3
"""Read vendor metadata and verify/extract Android bootstrap archives (stdlib only)."""
import argparse
import hashlib
import hmac
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tarfile
from urllib.parse import urljoin, urlparse
import xml.etree.ElementTree as ET
import zipfile


def local_name(tag):
    return tag.rsplit("}", 1)[-1]


def child_text(node, name):
    for child in node.iter():
        if local_name(child.tag) == name and child.text:
            return child.text.strip()
    raise ValueError("vendor metadata has no " + name)


def record(url, algorithm, checksum):
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.username or parsed.password:
        raise ValueError("archive URL must use HTTPS without credentials")
    if algorithm not in ("sha1", "sha256"):
        raise ValueError("unsupported vendor checksum algorithm: " + algorithm)
    size = 40 if algorithm == "sha1" else 64
    if not re.fullmatch("[0-9a-fA-F]{" + str(size) + "}", checksum):
        raise ValueError("invalid vendor checksum")
    if any(c in url for c in "\t\r\n"):
        raise ValueError("invalid archive URL")
    kind = "zip" if parsed.path.endswith(".zip") else "tar.gz"
    if kind == "tar.gz" and not parsed.path.endswith(".tar.gz"):
        raise ValueError("unsupported archive type")
    return "\t".join((url, algorithm, checksum.lower(), kind))


def sdk_record(filename, host, version):
    root = ET.parse(filename).getroot()
    for package in root.iter():
        if local_name(package.tag) != "remotePackage":
            continue
        if package.get("path") != "cmdline-tools;" + version:
            continue
        for archive in package.iter():
            if local_name(archive.tag) != "archive":
                continue
            if child_text(archive, "host-os") != host:
                continue
            url = urljoin("https://dl.google.com/android/repository/",
                          child_text(archive, "url"))
            if not url.startswith("https://dl.google.com/android/repository/"):
                raise ValueError("SDK archive is outside Google's repository")
            checksum = next(n for n in archive.iter()
                            if local_name(n.tag) == "checksum")
            value = (checksum.text or "").strip()
            algorithm = checksum.get("type", "sha1").lower().replace("-", "")
            return record(url, algorithm, value)
    raise ValueError("command-line tools " + version + " unavailable for " + host)


def jdk_record(filename):
    with open(filename, encoding="utf-8") as stream:
        assets = json.load(stream)
    for asset in assets:
        binary = asset.get("binary", {})
        if binary.get("image_type", "jdk") != "jdk":
            continue
        package = binary["package"]
        url = package["link"]
        if not url.startswith("https://github.com/adoptium/"):
            raise ValueError("Temurin archive is outside the official publisher")
        return record(url, "sha256", package["checksum"])
    raise ValueError("Adoptium returned no JDK archive")


def verify(filename, algorithm, expected):
    digest = hashlib.new(algorithm)
    with open(filename, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    if not hmac.compare_digest(digest.hexdigest(), expected.lower()):
        raise ValueError("checksum mismatch: " + str(filename))


def safe_path(root, name):
    if "\\" in name or ":" in name:
        raise ValueError("unsafe archive path: " + name)
    rel = PurePosixPath(name)
    if rel.is_absolute() or ".." in rel.parts:
        raise ValueError("unsafe archive path: " + name)
    path = root.joinpath(*rel.parts)
    if os.path.commonpath((str(root), str(path.resolve()))) != str(root):
        raise ValueError("archive path escapes extraction directory: " + name)
    return path


def extract(filename, destination):
    root = Path(destination).resolve()
    root.mkdir(parents=True, exist_ok=True)
    if zipfile.is_zipfile(filename):
        with zipfile.ZipFile(filename) as archive:
            for member in archive.infolist():
                path = safe_path(root, member.filename)
                mode = member.external_attr >> 16
                if mode & 0o170000 == 0o120000:
                    raise ValueError("unexpected ZIP symlink: " + member.filename)
                if member.is_dir():
                    path.mkdir(parents=True, exist_ok=True)
                    continue
                path.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(member) as source, path.open("wb") as target:
                    shutil.copyfileobj(source, target)
                if mode & 0o777:
                    path.chmod(mode & 0o777)
        return
    with tarfile.open(filename, "r:gz") as archive:
        for member in archive:
            path = safe_path(root, member.name)
            if member.isdir():
                path.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                path.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, path.open("wb") as target:
                    shutil.copyfileobj(source, target)
                path.chmod(member.mode & 0o777)
            elif member.issym() or member.islnk():
                base = path.parent if member.issym() else root
                # Resolve against the real filesystem, including previous links.
                if (PurePosixPath(member.linkname).is_absolute() or
                        "\\" in member.linkname or ":" in member.linkname):
                    raise ValueError("unsafe archive link: " + member.linkname)
                target = base.joinpath(member.linkname).resolve()
                if os.path.commonpath((str(root), str(target.resolve()))) != str(root):
                    raise ValueError("archive link escapes extraction directory")
                path.parent.mkdir(parents=True, exist_ok=True)
                if member.issym():
                    path.symlink_to(member.linkname)
                else:
                    os.link(target, path)
            else:
                raise ValueError("unsupported special archive entry: " + member.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("sdk", "jdk", "verify", "extract", "jdk-home"))
    parser.add_argument("args", nargs="+")
    args = parser.parse_args()
    if args.operation == "sdk":
        print(sdk_record(*args.args))
    elif args.operation == "jdk":
        print(jdk_record(*args.args))
    elif args.operation == "verify":
        verify(*args.args)
    elif args.operation == "extract":
        extract(*args.args)
    else:
        homes = [p.parent.parent for p in Path(args.args[0]).rglob("*")
                 if p.name in ("java", "java.exe") and p.parent.name == "bin" and p.is_file()]
        if len(homes) != 1:
            raise ValueError("expected one JDK home in verified archive")
        print(homes[0])


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, StopIteration, OSError, ET.ParseError,
            json.JSONDecodeError, tarfile.TarError, zipfile.BadZipFile) as error:
        raise SystemExit("toolchain metadata: " + str(error))
