#!/usr/bin/env python3
"""Validate, package and publish APK assets for one published GitHub release.

Signing stays in the workflow's isolated job; this helper never reads a key.
The API adapter uses gh's existing GH_TOKEN, with no token in command arguments.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import tomllib
from urllib.parse import quote
import zipfile

REPOSITORY = "psspssr/pokeemerald-3Ds-dualscreen-thor"
APPLICATION_ID = "com.emerald3ds.android"
SIGNER_SHA256 = "eeb95f89fcb944d3a62cc2aa8d0bb720584333d476c13b5a823ef486fdcd0389"
MAX_VERSION_CODE = 2_100_000_000
CORE_FILES = (
    "lib/armeabi-v7a/libemerald.so",
    "lib/armeabi-v7a/libemeraldboot.so",
    "assets/romfs/data.embedded",
    "assets/romfs/engine/abi.bin",
    "assets/romfs/graphics/party_menu/slot_wide_empty.bin",
    "assets/romfs/shaders/voxel.shbin",
)
SEMVER = re.compile(
    r"v(?P<version>(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
    r"(?:-(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?)"
)


class ReleaseError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise ReleaseError(message)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_hash(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def positive_id(value, label):
    require(type(value) is int and value > 0, "invalid " + label)
    return value


def versions(tag: str, run_number: str) -> tuple[str, int]:
    match = SEMVER.fullmatch(tag) if isinstance(tag, str) else None
    require(match is not None and len(tag) <= 100, "release tag must be strict vMAJOR.MINOR.PATCH[-prerelease][+build] SemVer")
    require(isinstance(run_number, str) and re.fullmatch(r"[1-9][0-9]*", run_number), "invalid GITHUB_RUN_NUMBER")
    code = int(run_number) + 1  # Code 1 belongs to the first manual release.
    require(code <= MAX_VERSION_CODE, "Android versionCode limit exceeded")
    return match["version"], code


class GitHub:
    def _run(self, args, **kwargs):
        result = subprocess.run(["gh", "api", *args], stderr=subprocess.PIPE, **kwargs)
        if result.returncode:
            raise ReleaseError("GitHub API failed: " + result.stderr.decode(errors="replace").strip())
        return result

    def get(self, endpoint):
        return json.loads(self._run([endpoint], stdout=subprocess.PIPE).stdout)

    def download(self, asset, target: Path):
        asset_id = positive_id(asset.get("id"), "asset ID")
        with target.open("wb") as stream:
            self._run([f"repos/{REPOSITORY}/releases/assets/{asset_id}",
                       "-H", "Accept: application/octet-stream"], stdout=stream)

    def upload(self, release_id, name, path: Path):
        endpoint = (f"https://uploads.github.com/repos/{REPOSITORY}/releases/"
                    f"{release_id}/assets?name={quote(name, safe='')}")
        return json.loads(self._run([endpoint, "--method", "POST", "--input", str(path),
                                    "-H", "Content-Type: application/octet-stream"],
                                   stdout=subprocess.PIPE).stdout)


def tag_commit(api, tag):
    obj = api.get(f"repos/{REPOSITORY}/git/ref/tags/{quote(tag, safe='')}")["object"]
    seen = set()
    for _ in range(8):
        oid = obj.get("sha")
        require(isinstance(oid, str) and re.fullmatch(r"[0-9a-f]{40}", oid), "invalid tag object SHA")
        require(oid not in seen, "cyclic annotated tag")
        seen.add(oid)
        if obj.get("type") == "commit":
            return oid
        require(obj.get("type") == "tag", "release tag does not identify a commit")
        obj = api.get(f"repos/{REPOSITORY}/git/tags/{oid}")["object"]
    raise ReleaseError("release tag nesting is too deep")


def context(api, event, run_number, source_sha, ref):
    require(event.get("action") == "published", "only release:published may publish APKs")
    require(event.get("repository", {}).get("full_name") == REPOSITORY, "wrong event repository")
    visibility = event.get("repository", {}).get("private")
    require(type(visibility) is bool, "release event repository visibility must be boolean")
    repo = api.get(f"repos/{REPOSITORY}")
    require(repo.get("full_name") == REPOSITORY and repo.get("default_branch") == "main",
            "expected release repository with main as default")
    require(type(repo.get("private")) is bool, "live repository visibility must be boolean")
    require(repo["private"] == visibility, "repository visibility changed after the release event")
    original = event.get("release", {})
    release_id = positive_id(original.get("id"), "release ID")
    tag = original.get("tag_name")
    version, code = versions(tag, run_number)
    require(ref == "refs/tags/" + tag, "workflow ref differs from the release tag")
    require(isinstance(source_sha, str) and re.fullmatch(r"[0-9a-f]{40}", source_sha), "invalid workflow source SHA")
    live = api.get(f"repos/{REPOSITORY}/releases/{release_id}")
    require(live.get("id") == release_id and live.get("tag_name") == tag,
            "release ID/tag changed after the event")
    require(original.get("draft") is False and live.get("draft") is False
            and original.get("published_at") and live.get("published_at"), "release is not published")
    require(live.get("target_commitish") == original.get("target_commitish"), "release target changed after the event")
    require(live.get("prerelease") == original.get("prerelease"), "release type changed after the event")
    commit = tag_commit(api, tag)
    require(commit == source_sha, "release tag moved or workflow source differs from the tag")
    target = live.get("target_commitish")
    require(target in ("main", commit), "release target must be main or the exact tagged commit")
    main = api.get(f"repos/{REPOSITORY}/git/ref/heads/main")["object"]
    require(main.get("type") == "commit", "invalid main branch object")
    comparison = api.get(f"repos/{REPOSITORY}/compare/{commit}...{main['sha']}")
    require(comparison.get("status") in ("ahead", "identical"), "tagged commit is not on main's history")
    plan = {"release_id": release_id, "release_tag": tag, "version": version,
            "version_code": code, "code_commit": commit,
            "apk": f"emerald-thor-{version}-armeabi-v7a.apk"}
    return plan, live


def asset_names(plan):
    return (plan["apk"], "build-info.json", "SHA256SUMS")


def selected_assets(release, plan):
    chosen = {}
    for asset in release.get("assets", []):
        name = asset.get("name")
        if name in asset_names(plan):
            require(name not in chosen, "duplicate release asset: " + name)
            chosen[name] = asset
    return chosen


def verify_manifest(info, plan):
    for key in ("release_id", "release_tag", "version", "version_code", "code_commit", "apk"):
        require(info.get(key) == plan[key], "existing build manifest differs: " + key)
    require(info.get("application_id") == APPLICATION_ID and info.get("debuggable") is False,
            "manifest is not the production release application")
    require(info.get("abi") == ["armeabi-v7a"] and info.get("min_sdk") == 28
            and info.get("target_sdk") == 35, "unexpected Android package requirements")
    require(info.get("signer_sha256") == SIGNER_SHA256, "manifest has the wrong signing identity")


def checksum_text(directory: Path, plan):
    return "".join(f"{file_hash(directory / name)}  {name}\n" for name in (plan["apk"], "build-info.json"))


def verify_bundle(directory: Path, plan):
    require({p.name for p in directory.iterdir()} == set(asset_names(plan)), "release bundle must contain only APK, manifest and checksums")
    require(all((directory / name).is_file() and not (directory / name).is_symlink()
                for name in asset_names(plan)), "release bundle contains a non-regular file")
    info = json.loads((directory / "build-info.json").read_text())
    verify_manifest(info, plan)
    apk = directory / plan["apk"]
    require(info.get("size") == apk.stat().st_size and info.get("sha256") == file_hash(apk), "APK does not match its build manifest")
    require((directory / "SHA256SUMS").read_text() == checksum_text(directory, plan), "release checksums differ")
    with zipfile.ZipFile(apk) as archive:
        require(len(archive.namelist()) == len(set(archive.namelist())), "duplicate APK ZIP members")
        packaged = info.get("packaged_sha256", {})
        require(set(packaged) == set(CORE_FILES), "incomplete native/assets manifest")
        for name in CORE_FILES:
            require(sha256(archive.read(name)) == packaged[name], "packaged bytes differ: " + name)
    return info


def inspect_apk(apk: Path, plan, build_tools: Path, signed=True):
    badging = subprocess.run([str(build_tools / "aapt"), "dump", "badging", str(apk)],
                             check=True, capture_output=True, text=True).stdout
    lines = badging.splitlines()
    package = next((line for line in lines if line.startswith("package: ")), "")
    fields = dict(item.split("=", 1) for item in shlex.split(package.removeprefix("package: ")))
    require(fields.get("name") == APPLICATION_ID and fields.get("versionName") == plan["version"]
            and fields.get("versionCode") == str(plan["version_code"]), "APK package/version differs from the release")
    require("application-debuggable" not in lines, "debuggable APK cannot be released")
    require("native-code: 'armeabi-v7a'" in lines, "unexpected APK ABI")
    require(any(line in ("sdkVersion:'28'", "minSdkVersion:'28'") for line in lines)
            and "targetSdkVersion:'35'" in lines,
            "APK SDK requirements changed")
    if signed:
        result = subprocess.run([str(build_tools / "apksigner"), "verify", "--verbose", "--print-certs", str(apk)],
                                check=True, capture_output=True, text=True).stdout
        digests = re.findall(r"Signer #[0-9]+ certificate SHA-256 digest: ([0-9a-f]+)", result)
        require(digests == [SIGNER_SHA256], "APK signer differs from the persistent release key")
        subprocess.run([str(build_tools / "zipalign"), "-c", "-P", "16", "4", str(apk)], check=True,
                       capture_output=True, text=True)


def preflight(api, event, run_number, source_sha, ref):
    plan, release = context(api, event, run_number, source_sha, ref)
    assets = selected_assets(release, plan)
    if not assets:
        return {**plan, "mode": "build"}
    require(set(assets) == set(asset_names(plan)),
            "partial release assets exist; re-run the failed publish job to reuse its exact signed bundle; nothing was overwritten")
    with tempfile.TemporaryDirectory(prefix="emerald-existing-release-") as folder:
        directory = Path(folder)
        for name, asset in assets.items():
            api.download(asset, directory / name)
        verify_bundle(directory, plan)
    return {**plan, "mode": "existing"}


def prepare_unsigned(root: Path, native: Path, apk: Path, output: Path, plan, build_tools: Path, run_id):
    inspect_apk(apk, plan, build_tools, signed=False)
    require(subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, check=True,
                           capture_output=True, text=True).stdout.strip() == plan["code_commit"],
            "build checkout differs from the tagged source")
    checks = {}
    with zipfile.ZipFile(apk) as archive:
        require(len(archive.namelist()) == len(set(archive.namelist())), "duplicate APK ZIP members")
        for name in CORE_FILES:
            expected = native / ("jniLibs/" + name[4:] if name.startswith("lib/") else name)
            data = archive.read(name)
            require(data == expected.read_bytes(), "APK differs from native output: " + name)
            checks[name] = sha256(data)
        abi = archive.read("assets/romfs/engine/abi.bin")
    require(len(abi) == 4, "unexpected engine ABI marker format")
    origin = tomllib.loads((root / "origin.lock").read_text())["origin"]
    pret = tomllib.loads((root / "origin/upstream.lock").read_text())["pokeemerald"]
    info = {**plan, "application_id": APPLICATION_ID, "debuggable": False,
            "abi": ["armeabi-v7a"], "min_sdk": 28, "target_sdk": 35,
            "upstream_commit": origin["commit"], "pret_commit": pret["commit"],
            "engine_abi": f"{int.from_bytes(abi, 'little'):08x}", "packaged_sha256": checks,
            "unsigned_sha256": file_hash(apk),
            "ci": f"https://github.com/{REPOSITORY}/actions/runs/{run_id}",
            "tool_versions": {"ndk": "27.2.12479018", "build_tools": "35.0.0",
                              "jdk": "17", "gradle": "8.10.2", "agp": "8.7.3", "kotlin": "2.0.21"},
            "validation": "Reusable source/host, native and emulator checks passed; full ARM gameplay acceptance remains a separate maintainer check."}
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(apk, output / "unsigned.apk")
    (output / "build-input.json").write_text(json.dumps(info, indent=2, sort_keys=True) + "\n")
    return info


def publish(api, event, run_number, source_sha, ref, directory: Path):
    plan, release = context(api, event, run_number, source_sha, ref)
    verify_bundle(directory, plan)
    # Detect all existing conflicts before writing even the first asset.
    with tempfile.TemporaryDirectory(prefix="emerald-preupload-check-") as folder:
        for name, asset in selected_assets(release, plan).items():
            downloaded = Path(folder) / name
            api.download(asset, downloaded)
            require(downloaded.read_bytes() == (directory / name).read_bytes(),
                    "existing release asset differs; refusing to overwrite " + name)
    # Manifest first binds any interrupted upload to its source/version. A
    # failed-job rerun gets the original signed bundle by immutable artifact ID.
    for name in ("build-info.json", plan["apk"], "SHA256SUMS"):
        current, release = context(api, event, run_number, source_sha, ref)
        require(current == plan, "release identity changed while publishing")
        existing = selected_assets(release, plan).get(name)
        if existing:
            with tempfile.TemporaryDirectory(prefix="emerald-asset-check-") as folder:
                downloaded = Path(folder) / name
                api.download(existing, downloaded)
                require(downloaded.read_bytes() == (directory / name).read_bytes(),
                        "existing release asset differs; refusing to overwrite " + name)
        else:
            api.upload(plan["release_id"], name, directory / name)
    current, release = context(api, event, run_number, source_sha, ref)
    require(current == plan, "release identity changed after upload")
    assets = selected_assets(release, plan)
    require(set(assets) == set(asset_names(plan)), "release upload is incomplete")
    with tempfile.TemporaryDirectory(prefix="emerald-release-download-") as folder:
        downloaded = Path(folder)
        for name, asset in assets.items():
            api.download(asset, downloaded / name)
            require((downloaded / name).read_bytes() == (directory / name).read_bytes(),
                    "uploaded bytes differ: " + name)
        verify_bundle(downloaded, plan)


def outputs(path: Path | None, values):
    if path:
        with path.open("a") as stream:
            for key, value in values.items():
                require("\n" not in str(value), "invalid multiline workflow output")
                stream.write(f"{key}={value}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "prepare", "verify-existing", "publish"))
    parser.add_argument("--event", type=Path, default=os.environ.get("GITHUB_EVENT_PATH"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--github-output", type=Path)
    parser.add_argument("--native-out", type=Path)
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--build-tools", type=Path)
    args = parser.parse_args()
    require(os.environ.get("GITHUB_REPOSITORY") == REPOSITORY
            and os.environ.get("GITHUB_EVENT_NAME") == "release", "release workflow context is required")
    require(args.event is not None, "release event payload is required")
    event = json.loads(args.event.read_text())
    params = (event, os.environ.get("GITHUB_RUN_NUMBER"), os.environ.get("GITHUB_SHA"), os.environ.get("GITHUB_REF"))
    api = GitHub()
    if args.command == "plan":
        plan = preflight(api, *params)
        require(args.output is not None, "--output is required")
        args.output.write_text(json.dumps(plan, indent=2) + "\n")
        outputs(args.github_output, plan)
        print(f"Release {plan['release_id']}: {plan['release_tag']} at {plan['code_commit']}, Android code {plan['version_code']}, {plan['mode']}")
    elif args.command == "prepare":
        plan, _ = context(api, *params)
        require(all((args.native_out, args.apk, args.output, args.build_tools)), "prepare needs native output, APK, output and build tools")
        info = prepare_unsigned(Path(__file__).resolve().parents[1], args.native_out, args.apk,
                                args.output, plan, args.build_tools, os.environ["GITHUB_RUN_ID"])
        outputs(args.github_output, {"unsigned_sha256": info["unsigned_sha256"]})
    elif args.command == "verify-existing":
        plan, release = context(api, *params)
        require(args.build_tools is not None, "--build-tools is required")
        assets = selected_assets(release, plan)
        require(set(assets) == set(asset_names(plan)), "release is incomplete")
        with tempfile.TemporaryDirectory(prefix="emerald-existing-verify-") as folder:
            directory = Path(folder)
            for name, asset in assets.items():
                api.download(asset, directory / name)
            verify_bundle(directory, plan)
            inspect_apk(directory / plan["apk"], plan, args.build_tools)
        print("Existing signed release assets verified; nothing rebuilt or replaced")
    else:
        plan, _ = context(api, *params)
        require(args.bundle is not None and args.build_tools is not None, "publish needs bundle and build tools")
        verify_bundle(args.bundle, plan)
        inspect_apk(args.bundle / plan["apk"], plan, args.build_tools)
        publish(api, *params, args.bundle)
        print(f"Verified exact uploaded assets on release ID {plan['release_id']}")


if __name__ == "__main__":
    try:
        main()
    except (ReleaseError, OSError, ValueError, KeyError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        raise SystemExit("release: " + str(error)) from error
