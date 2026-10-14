#!/usr/bin/env python3
"""Check a dated dependency gap inventory offline, without granting rights.

Default checks need only tracked inputs. --local-evidence additionally checks
the selected archives and raw installed receipts; it never searches caches,
imports backends, installs packages or fetches URLs. A valid inventory can have
open gaps. --require-closure returns 3 for those gaps, rather than claiming a pass.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import tarfile

MAX_JSON = 16 * 1024**2
MAX_ARCHIVE = 32 * 1024**2
MAX_NOTICE = 1024**2
MAX_MEMBERS = 50000
MAX_EXPANDED = 256 * 1024**2
REQUIRED_PROFILES = frozenset({"native-development-archives", "training", "poses", "metrics-cpu",
                               "native-system-and-build-tools", "release-vcpkg-declaration", "metric-models"})


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_relative(relative):
    path = PurePosixPath(relative)
    if (not relative or path.is_absolute() or ".." in path.parts or "\\" in relative
            or "\0" in relative or str(path) != relative):
        raise ValueError("Invalid repository-relative evidence path")
    return path


def contained(root, relative):
    safe_relative(relative)
    result = (root / relative).resolve(strict=True)
    if not result.is_relative_to(root.resolve(strict=True)) or not result.is_file():
        raise ValueError("Evidence path escapes repository or is not a regular file")
    return result


def read_file(path, maximum=MAX_JSON):
    before = path.stat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > maximum:
        raise ValueError("Evidence exceeds byte budget")
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    after = path.stat()
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
    if len(data) > maximum or len(data) != before.st_size or identity(before) != identity(after):
        raise ValueError("Evidence changed or exceeded byte budget")
    return data


def load_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate JSON key")
            result[key] = value
        return result

    def invalid_constant(value):
        raise ValueError("Non-finite JSON constant: " + value)

    return json.loads(read_file(path), object_pairs_hook=unique, parse_constant=invalid_constant)


def archive_notice(path, member, expected_sha256=None):
    # Hash verification happens before this bounded scan. Never extract paths.
    found = None
    expanded = 0
    data = read_file(path, MAX_ARCHIVE)
    if expected_sha256 is not None and digest(data) != expected_sha256:
        raise ValueError("Archive changed before notice scan")
    with io.BytesIO(data) as source, tarfile.open(fileobj=source, mode="r|gz") as archive:
        for count, entry in enumerate(archive, 1):
            expanded += entry.size
            if count > MAX_MEMBERS or expanded > MAX_EXPANDED or entry.size < 0:
                raise ValueError("Archive exceeds inventory budget")
            if entry.name == member:
                if found is not None or not entry.isfile() or entry.size > MAX_NOTICE:
                    raise ValueError("Ambiguous, linked or over-budget archive notice")
                stream = archive.extractfile(entry)
                if stream is None:
                    raise ValueError("Archive notice is unreadable")
                with stream:
                    found = stream.read(MAX_NOTICE + 1)
                if len(found) != entry.size:
                    raise ValueError("Archive notice is truncated")
    if found is None:
        raise ValueError("Selected notice absent from pinned archive")
    return found


def compare_notice(original, retained, policy):
    if policy == "REMOVE_ONE_FIRST_LINE_TRAILING_SPACE":
        first, separator, rest = original.partition(b"\n")
        if not separator or not first.endswith(b" "):
            raise ValueError("Declared notice whitespace normalization is inapplicable")
        original = first[:-1] + separator + rest
    elif policy != "EXACT":
        raise ValueError("Unsupported notice normalization")
    if original != retained:
        raise ValueError("Retained notice differs from pinned archive")


def receipt_packages(receipt):
    environment = receipt.get("environment", receipt)
    if (environment.get("schema_version") != 1 or not environment.get("requirements_match")
            or environment.get("deviations") or environment.get("unlisted_distributions")
            or environment.get("uninspected_legacy_metadata")):
        raise ValueError("Installed receipt has unresolved identity deviations")
    packages = environment["packages"]
    if not isinstance(packages, list) or len(packages) > 256:
        raise ValueError("Invalid installed package inventory")
    if len({package["name"] for package in packages}) != len(packages):
        raise ValueError("Duplicate installed package")
    for package in packages:
        if package["integrity"] != "VERIFIED_RECORD" or package["issues"]:
            raise ValueError("Unverified installed package")
        for field, inventory in (("installed_files_sha256", package["files"]),
                                 ("notice_inventory_sha256", package["notices"])):
            actual = digest(canonical(inventory))
            if field in package and package[field] != actual:
                raise ValueError("Installed receipt inventory digest differs")
    return environment


def check_manifest(root, manifest, local_evidence=False):
    if (manifest.get("schema_version") != 1 or manifest.get("rights_qualified") is not False
            or manifest.get("status") != "IN_PROGRESS"):
        raise ValueError("Unsupported inventory schema or qualification claim")
    inputs = manifest["inputs"]
    profiles = manifest["profiles"]
    if not isinstance(inputs, list) or len(inputs) > 256 or not isinstance(profiles, list) or len(profiles) > 16:
        raise ValueError("Inventory exceeds input/profile budget")
    if {profile["id"] for profile in profiles} != REQUIRED_PROFILES:
        raise ValueError("Selected profile inventory is incomplete or unsupported")
    bindings = {}
    for entry in inputs:
        path = entry["path"]
        safe_relative(path)
        if path in bindings or entry["kind"] not in {"tracked", "local"}:
            raise ValueError("Duplicate or unsupported evidence binding")
        if not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise ValueError("Invalid evidence digest")
        maximum = MAX_ARCHIVE if entry.get("archive") else MAX_JSON
        if type(entry["bytes"]) is not int or not 0 <= entry["bytes"] <= maximum:
            raise ValueError("Invalid evidence byte count")
        bindings[path] = entry
        if entry["kind"] == "tracked" or local_evidence:
            data = read_file(contained(root, path), maximum)
            if digest(data) != entry["sha256"] or len(data) != entry["bytes"]:
                raise ValueError("Changed evidence: " + path)
    names = set()
    gaps = 0
    components = 0
    for comparison in manifest.get("comparisons", []):
        left, right = comparison["left"], comparison["right"]
        if left not in bindings or right not in bindings or bindings[left]["sha256"] != bindings[right]["sha256"]:
            raise ValueError("Source/notice comparison is unbound or differs")
    for profile in profiles:
        if profile["id"] in names:
            raise ValueError("Duplicate profile")
        names.add(profile["id"])
        rows = profile["components"]
        if not rows or len(rows) > 256 or len({row["name"] for row in rows}) != len(rows):
            raise ValueError("Empty, duplicate or over-budget component inventory")
        components += len(rows)
        for row in rows:
            if (row["rights"] != "NOT_REVIEWED" or "TRANSITIVE_RIGHTS_UNREVIEWED" not in row["gaps"]
                    or not row["source"] or not row["archive"] or not row["notice"]):
                raise ValueError("Missing source/archive/notice or rights gap")
            gaps += len(row["gaps"])
        if profile["kind"] == "worker":
            selection = load_json(contained(root, profile["selection"]))
            if profile["selection"] not in bindings or profile["requirements"] not in bindings:
                raise ValueError("Unbound worker selection")
            if profile["id"] == "metrics-cpu":
                selected = selection["identity"]["packages"]
            else:
                selected = selection["environments"][profile["id"]]["packages"]
            expected = {row["name"]: row for row in selected}
            if set(expected) != {row["name"] for row in rows}:
                raise ValueError("Worker component inventory is incomplete")
            for row in rows:
                reference = expected[row["name"]]
                for field in ("version", "metadata_sha256", "record_sha256", "installed_files_sha256"):
                    actual = row["version"] if field == "version" else row["installed"][field]
                    if reference[field] != actual:
                        raise ValueError("Worker selection identity differs")
            if local_evidence:
                if profile["receipt"] not in bindings:
                    raise ValueError("Unbound installed receipt")
                environment = receipt_packages(load_json(contained(root, profile["receipt"])))
                if environment["requirements_sha256"] != bindings[profile["requirements"]]["sha256"]:
                    raise ValueError("Receipt requirements differ")
                actual = {row["name"]: row for row in environment["packages"]}
                if set(actual) != set(expected):
                    raise ValueError("Raw receipt package inventory differs")
                for row in rows:
                    package = actual[row["name"]]
                    for field, value in row["installed"].items():
                        if package[field] != value:
                            raise ValueError("Raw receipt installed identity differs")
                    if (len(package["notices"]) != row["notice"]["files"]
                            or digest(canonical(package["notices"])) != row["notice"]["inventory_sha256"]):
                        raise ValueError("Raw receipt notice inventory differs")
        elif profile["kind"] == "native-archives":
            if "cmake/dependencies.cmake" not in bindings:
                raise ValueError("Unbound native dependency declaration")
            cmake = read_file(contained(root, "cmake/dependencies.cmake")).decode()
            declared = re.findall(r"we_archive\((\w+) (\S+) (https://\S+) ([0-9a-f]{64})\)", cmake)
            if {item[0] for item in declared} != {row["name"] for row in rows}:
                raise ValueError("Native archive inventory is incomplete")
            for name, filename, url, sha256 in declared:
                row = next(row for row in rows if row["name"] == name)
                if (row["source"]["url"] != url or row["archive"]["sha256"] != sha256
                        or row["version"] != url.rsplit("/", 1)[1].removeprefix("v")):
                    raise ValueError("Native archive declaration differs")
                if row["notice"]["path"] not in bindings:
                    raise ValueError("Unbound retained native notice")
                if row["notice"]["sha256"] != bindings[row["notice"]["path"]]["sha256"]:
                    raise ValueError("Retained native notice identity differs")
                if local_evidence:
                    path = ".deps/" + filename
                    if path not in bindings or bindings[path]["sha256"] != sha256:
                        raise ValueError("Unbound native archive")
                    notice = archive_notice(contained(root, path), row["notice"]["member"], sha256)
                    retained = read_file(contained(root, row["notice"]["path"]), MAX_NOTICE)
                    if digest(notice) != row["notice"]["archive_sha256"]:
                        raise ValueError("Archive notice digest differs")
                    compare_notice(notice, retained, row["notice"]["comparison"])
        elif profile["kind"] == "release-declaration":
            if "vcpkg.json" not in bindings or "vcpkg-configuration.json" not in bindings:
                raise ValueError("Unbound release dependency declaration")
            declaration = load_json(contained(root, "vcpkg.json"))
            expected = {entry if isinstance(entry, str) else entry["name"]
                        for entry in declaration["dependencies"]}
            if expected != {row["name"] for row in rows}:
                raise ValueError("Release declaration inventory is incomplete")
            registry = load_json(contained(root, "vcpkg-configuration.json"))["default-registry"]
            overrides = {entry["name"]: entry["version"] for entry in declaration.get("overrides", [])}
            for row in rows:
                if (row["source"]["baseline"] != registry["baseline"]
                        or row["source"]["registry"] != registry["repository"]
                        or row["version"] != overrides.get(row["name"])):
                    raise ValueError("Release registry/version declaration differs")
        elif profile["kind"] == "system-observation":
            if profile["evidence"] not in bindings:
                raise ValueError("Unbound system observation")
            if local_evidence:
                observed = {}
                for block in read_file(contained(root, profile["evidence"])).decode().split("\n\n"):
                    fields = dict(re.findall(r"^(Name|Version|URL|Licenses)\s+: (.*)$", block, re.M))
                    if fields:
                        if fields["Name"] in observed:
                            raise ValueError("Duplicate system package observation")
                        observed[fields["Name"]] = fields
                if set(observed) != {row["name"] for row in rows}:
                    raise ValueError("System observation inventory is incomplete")
                for row in rows:
                    reference = observed[row["name"]]
                    if (row["version"] != reference["Version"] or row["source"]["url"] != reference["URL"]
                            or row["notice"]["declared_license"] != reference["Licenses"]):
                        raise ValueError("System package observation differs")
        elif profile["kind"] not in {"system-observation", "model"}:
            raise ValueError("Unsupported profile kind")
    return {"profiles": len(profiles), "components": components, "open_gaps": gaps,
            "local_evidence_checked": local_evidence, "rights_qualified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path("third_party/dependency-audit-20261014.json"))
    parser.add_argument("--local-evidence", action="store_true")
    parser.add_argument("--require-closure", action="store_true")
    args = parser.parse_args()
    try:
        result = check_manifest(Path(__file__).resolve().parents[1], load_json(args.manifest), args.local_evidence)
        print(json.dumps(result, sort_keys=True))
        return 3 if args.require_closure and result["open_gaps"] else 0
    except (OSError, ValueError, KeyError, TypeError, tarfile.TarError) as error:
        print("Dependency inventory check failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
