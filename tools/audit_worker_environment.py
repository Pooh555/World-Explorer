#!/usr/bin/env python3
"""Record installed worker bytes/notices offline; never import backend packages.

Run with the selected worker environment's Python. This is an installed-file
receipt, not a wheel archive lock, full SBOM or rights/metric clearance.
"""

import argparse
import base64
import csv
from email.parser import BytesParser
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import sys
import sysconfig
import tempfile
from urllib.parse import unquote, urlsplit

MAX_METADATA_BYTES = 16 * 1024**2
MAX_FILE_BYTES = 4 * 1024**3
MAX_TOTAL_BYTES = 32 * 1024**3
MAX_FILES = 100000
MAX_DISTRIBUTIONS = 256


def canonical_bytes(value):
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False
    ).encode()


def normalize_name(name):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}", name):
        raise ValueError("Invalid distribution name")
    return re.sub(r"[-_.]+", "-", name).lower()


def file_digests(path, maximum, algorithms):
    before = path.stat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > maximum:
        raise ValueError("File is not regular or exceeds audit byte limit")
    digests = {algorithm: hashlib.new(algorithm) for algorithm in algorithms}
    count = 0
    with path.open("rb") as stream:
        while block := stream.read(1024**2):
            count += len(block)
            if count > maximum:
                raise ValueError("File grew past audit byte limit")
            for digest in digests.values():
                digest.update(block)
    after = path.stat()
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
    if count != before.st_size or identity(before) != identity(after):
        raise ValueError("File changed during environment audit")
    return {
        algorithm: digest.hexdigest() for algorithm, digest in digests.items()
    }, count


def file_digest(path, maximum=MAX_FILE_BYTES):
    digests, count = file_digests(path, maximum, ("sha256",))
    return digests["sha256"], count


def bounded_read(path):
    if path.stat().st_size > MAX_METADATA_BYTES or not path.is_file():
        raise ValueError("Metadata exceeds audit limit or is not regular")
    with path.open("rb") as stream:
        data = stream.read(MAX_METADATA_BYTES + 1)
    if len(data) > MAX_METADATA_BYTES:
        raise ValueError("Metadata grew past audit byte limit")
    return data


def contained(path, prefix):
    selected = path.resolve(strict=True)
    if not selected.is_relative_to(prefix):
        raise ValueError("Installed-file reference escapes selected environment")
    return selected


def read_requirements(path):
    pins = {}
    data = bounded_read(path)
    for line in data.decode("utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        exact = re.fullmatch(r"([A-Za-z0-9_.-]+)==([A-Za-z0-9_.+!-]+)", line)
        if exact:
            name, version = exact.groups()
        else:
            direct = re.fullmatch(r"([A-Za-z0-9_.-]+) @ (https://\S+)", line)
            if not direct:
                raise ValueError("Audit requires exact versions or a direct wheel URL")
            name, url = direct.groups()
            parts = unquote(Path(urlsplit(url).path).name).split("-")
            if len(parts) != 5 or not parts[-1].endswith(".whl"):
                raise ValueError("Direct requirement must identify a wheel version")
            if normalize_name(parts[0]) != normalize_name(name):
                raise ValueError("Wheel filename disagrees with requirement name")
            version = parts[1]
        name = normalize_name(name)
        if name in pins or len(pins) >= MAX_DISTRIBUTIONS:
            raise ValueError("Duplicate or over-budget requirements")
        pins[name] = version
    if not pins:
        raise ValueError("Empty worker requirements")
    return pins, hashlib.sha256(data).hexdigest()


def audit_distribution(directory, prefix, budget):
    directory = contained(directory, prefix)
    metadata_path = contained(directory / "METADATA", prefix)
    metadata_bytes = bounded_read(metadata_path)
    fields = BytesParser().parsebytes(metadata_bytes)
    name = normalize_name(str(fields.get("Name", "")))
    version = str(fields.get("Version", ""))
    if not re.fullmatch(r"[A-Za-z0-9_.+!-]{1,128}", version):
        raise ValueError("Invalid installed distribution version")
    record_path = directory / "RECORD"
    result = {
        "name": name,
        "version": version,
        "metadata_sha256": hashlib.sha256(metadata_bytes).hexdigest(),
        "license_metadata_sha256": hashlib.sha256(
            canonical_bytes(
                {
                    key: [str(value) for value in fields.get_all(key, [])]
                    for key in [
                        "License",
                        "License-Expression",
                        "License-File",
                        "Classifier",
                    ]
                }
            )
        ).hexdigest(),
        "wheel_archive_sha256": None,
        "notices": [],
    }
    if not record_path.is_file():
        result.update(
            integrity="UNVERIFIED_NO_RECORD",
            files=[],
            record_sha256=None,
            installed_files_sha256=None,
            issues=["Installed RECORD absent"],
        )
        return result
    record_path = contained(record_path, prefix)
    record_bytes = bounded_read(record_path)
    result["record_sha256"] = hashlib.sha256(record_bytes).hexdigest()
    files, issues, paths = [], [], set()
    for entry in csv.reader(io.StringIO(record_bytes.decode("utf-8")), strict=True):
        if len(entry) != 3 or not entry[0] or len(entry[0]) > 4096 or "\0" in entry[0]:
            raise ValueError("Invalid installed RECORD row")
        path = contained(directory.parent / entry[0], prefix)
        relative = path.relative_to(prefix).as_posix()
        if relative in paths or len(files) >= MAX_FILES or budget["files"] >= MAX_FILES:
            raise ValueError("Duplicate or over-budget installed-file inventory")
        paths.add(relative)
        algorithm, expected = "sha256", None
        if entry[1]:
            algorithm, separator, expected = entry[1].partition("=")
            if not separator or algorithm not in {"sha256", "sha384", "sha512"}:
                raise ValueError("Unsupported or malformed RECORD digest")
        maximum = min(MAX_FILE_BYTES, MAX_TOTAL_BYTES - budget["bytes"])
        digests, size = file_digests(path, maximum, {"sha256", algorithm})
        budget["bytes"] += size
        budget["files"] += 1
        row = {
            "path": relative,
            "bytes": size,
            "sha256": digests["sha256"],
            "record_check": "NO_HASH",
        }
        if entry[2] and (
            not re.fullmatch(r"[0-9]{1,20}", entry[2]) or int(entry[2]) != size
        ):
            issues.append(relative + ": RECORD size differs")
        if expected is not None:
            actual = digests[algorithm]
            encoded = (
                base64.urlsafe_b64encode(bytes.fromhex(actual)).decode().rstrip("=")
            )
            row["record_check"] = "MATCH" if encoded == expected else "MISMATCH"
            if row["record_check"] == "MISMATCH":
                issues.append(relative + ": RECORD digest differs")
        elif path != record_path and path.suffix != ".pyc":
            issues.append(relative + ": RECORD has no digest")
        if any(
            token in path.name.lower() for token in ("license", "notice", "copying")
        ):
            result["notices"].append(
                {key: row[key] for key in ("path", "bytes", "sha256")}
            )
        files.append(row)
    if record_path.relative_to(prefix).as_posix() not in paths:
        issues.append("RECORD does not list itself")
    if metadata_path.relative_to(prefix).as_posix() not in paths:
        issues.append("RECORD does not list METADATA")
    if hashlib.sha256(bounded_read(record_path)).hexdigest() != result["record_sha256"]:
        raise ValueError("RECORD changed during audit")
    if bounded_read(metadata_path) != metadata_bytes:
        raise ValueError("METADATA changed during audit")
    files.sort(key=lambda item: item["path"])
    result.update(
        files=files,
        issues=issues,
        integrity="VERIFIED_RECORD" if not issues else "UNVERIFIED_RECORD",
        installed_files_sha256=hashlib.sha256(canonical_bytes(files)).hexdigest(),
    )
    return result


def audit_environment(prefix, sites, requirements):
    prefix = prefix.resolve(strict=True)
    pins, requirements_sha256 = read_requirements(requirements)
    budget = {"files": 0, "bytes": 0}
    directories, uninspected = set(), set()
    for site in sites:
        site = contained(site, prefix)
        for directory in site.glob("*.dist-info"):
            directories.add(directory)
            if len(directories) > MAX_DISTRIBUTIONS:
                raise ValueError("Installed distribution count exceeds audit limit")
        for path in site.glob("*.egg-info"):
            uninspected.add(path.relative_to(prefix).as_posix())
            if len(uninspected) > MAX_DISTRIBUTIONS:
                raise ValueError("Legacy metadata count exceeds audit limit")
    packages = [
        audit_distribution(path, prefix, budget) for path in sorted(directories)
    ]
    installed = {package["name"]: package for package in packages}
    if len(installed) != len(packages):
        raise ValueError("Ambiguous duplicate installed distribution")
    deviations = []
    for name, expected in pins.items():
        actual = installed.get(name, {}).get("version")
        if actual != expected:
            deviations.append({"name": name, "expected": expected, "installed": actual})
    if hashlib.sha256(bounded_read(requirements)).hexdigest() != requirements_sha256:
        raise ValueError("Requirements changed during audit")
    return {
        "schema_version": 1,
        "scope": "installed RECORD-listed bytes; not archive or rights lock",
        "python_version": sys.version.split()[0],
        "platform": sys.platform,
        "requirements_sha256": requirements_sha256,
        "requirements_match": not deviations,
        "deviations": deviations,
        "unlisted_distributions": sorted(set(installed) - set(pins)),
        "uninspected_legacy_metadata": sorted(uninspected),
        "packages": packages,
        "listed_files": budget["files"],
        "listed_bytes": budget["bytes"],
        "metrics": {
            name: installed.get(name, {}).get("version")
            for name in ("torchmetrics", "lpips")
        },
        "metric_qualified": False,
        "backbone_sha256": None,
        "rights_clearance": "NOT_PERFORMED",
        "wheel_archive_lock": "NOT_AVAILABLE",
    }


def publish_receipt(path, report):
    data = (
        json.dumps(report, sort_keys=True, indent=2, allow_nan=False).encode() + b"\n"
    )
    if len(data) > MAX_METADATA_BYTES:
        raise ValueError("Environment receipt exceeds output budget")
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".worker-audit-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, path)  # Atomic creation; never replace an existing receipt.
    finally:
        Path(temporary).unlink(missing_ok=True)


def record_backbone(report, path):
    # Hash explicitly selected bytes only; never deserialize a checkpoint or
    # search caches, infer rights, initialize a model or fetch missing weights.
    if path is not None:
        digest, size = file_digest(path)
        report["backbone_sha256"] = digest
        report["backbone_bytes"] = size
        report["backbone_status"] = "LOCAL_BYTES_UNREVIEWED"
    else:
        report["backbone_status"] = "NOT_SELECTED"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--requirements", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--alexnet-weights",
        type=Path,
        help="Explicit local bytes to hash; no model loading or rights inference",
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        output = args.output.resolve()
        if not output.is_relative_to(root / "artifacts") or args.output.exists():
            raise ValueError("Select a new receipt path inside ignored artifacts/")
        if sys.prefix == sys.base_prefix:
            raise ValueError(
                "Run the audit with the explicitly selected worker virtual environment"
            )
        sites = {Path(sysconfig.get_path(key)) for key in ("purelib", "platlib")}
        report = audit_environment(Path(sys.prefix), sites, args.requirements)
        record_backbone(report, args.alexnet_weights)
        report["auditor_sha256"] = file_digest(Path(__file__), MAX_METADATA_BYTES)[0]
        publish_receipt(output, report)
        print(
            json.dumps(
                {
                    "receipt": str(output.relative_to(root)),
                    "packages": len(report["packages"]),
                    "listed_files": report["listed_files"],
                    "requirements_match": report["requirements_match"],
                    "metric_qualified": False,
                }
            )
        )
        return 0
    except (OSError, ValueError, csv.Error) as error:
        print(f"Worker environment audit failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
