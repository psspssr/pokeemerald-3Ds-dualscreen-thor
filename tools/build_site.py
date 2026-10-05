#!/usr/bin/env python3
"""Build the static Pages site; resolve published releases at build time.

Live: GH_TOKEN or GITHUB_TOKEN needs read access to the release repository.
Offline: --snapshot site/release-snapshot.json uses an explicitly pinned feed.
Only allowlisted display metadata and links reach the output, never API
responses, credentials, application binaries or game data.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import html
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
from urllib.error import HTTPError, URLError
from urllib.parse import quote, unquote, urlsplit
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPOSITORY = "psspssr/pokeemerald-3Ds-dualscreen-thor"
TAG = re.compile(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?\Z")
MEDIA = {
    "thor-presentation.png": "docs/images/thor-presentation.png",
    "classic-town.png": "docs/images/classic-town.png",
    "classic-route.png": "docs/images/classic-route.png",
    "voxel-world.png": "docs/images/voxel-world.png",
    "battle.png": "docs/images/battle.png",
    "classic-route-beta.png": "docs/images/classic-route-beta.png",
    "voxel-sharp-2x.png": "docs/images/voxel-sharp-2x.png",
    "voxel-battle-beta.png": "docs/images/voxel-battle-beta.png",
    "thor-battle-touch.png": "docs/images/thor-battle-touch.png",
    "portrait-controls.png": "docs/images/portrait-controls.png",
    "app-icon.png": "android/app/src/main/res/drawable-nodpi/ic_launcher_art.png",
}
TEMPLATES = ("index.html", "credits.html")
STATIC_FILES = ("styles.css", "site.js")
FONT_FILES = ("pixelify-sans.ttf", "OFL.txt", "SOURCE.txt")
ARTIFACT_FILES = frozenset({
    *TEMPLATES, *STATIC_FILES, "release.json", ".nojekyll", ".emerald-site",
    *("assets/" + name for name in MEDIA),
    *("assets/fonts/" + name for name in FONT_FILES),
})
ARTIFACT_DIRECTORIES = frozenset(
    parent.as_posix() for name in ARTIFACT_FILES for parent in Path(name).parents
    if parent != Path(".")
)


class SiteError(ValueError):
    pass


def source_file(root: Path, relative: str) -> Path:
    """Read only an explicitly named regular source, never a symlink/escape."""
    root = root.resolve()
    name = Path(relative)
    if name.is_absolute() or ".." in name.parts:
        raise SiteError("website source must stay inside the source tree: " + relative)
    source = root / name
    for part in (source, *source.parents):
        if part == root:
            break
        if part.is_symlink():
            raise SiteError("website source must not use symbolic links: " + relative)
    if not source.resolve().is_relative_to(root) or not source.is_file():
        raise SiteError("missing or external regular website source: " + relative)
    return source


def validate_artifact(output: Path) -> None:
    """Apply the same closed, recursive file manifest before build/publish."""
    if output.is_symlink() or not output.is_dir():
        raise SiteError("website artifact must be a regular directory")
    found = set()
    pending = [output]
    while pending:
        for item in pending.pop().iterdir():
            relative = item.relative_to(output).as_posix()
            if item.is_symlink():
                raise SiteError("website artifact must not contain symbolic links")
            if item.is_dir() and relative in ARTIFACT_DIRECTORIES:
                pending.append(item)
            elif item.is_file() and relative in ARTIFACT_FILES:
                found.add(relative)
            else:
                raise SiteError("unexpected website artifact path: " + relative)
    if found != ARTIFACT_FILES:
        raise SiteError("website artifact is missing expected files")


def repository_name(value: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9][A-Za-z0-9_.-]*", value):
        raise SiteError("expected a GitHub owner/repository name")
    return value


def same_github_url(value: object, expected: str) -> bool:
    if not isinstance(value, str):
        return False
    try:
        actual, wanted = urlsplit(value), urlsplit(expected)
    except ValueError:
        return False
    return (actual.scheme == "https" and actual.netloc == "github.com" and
            not actual.query and not actual.fragment and
            unquote(actual.path) == unquote(wanted.path))


def select_release(repository: dict, releases: list[dict], expected_repo: str) -> dict:
    """Include prereleases, skip drafts/incomplete uploads, and sort by publication."""
    repo = repository_name(expected_repo)
    if repository.get("full_name", "").lower() != repo.lower() or type(repository.get("private")) is not bool:
        raise SiteError("repository identity or visibility is missing")
    complete = []
    for release in releases:
        if release.get("draft") is not False or type(release.get("prerelease")) is not bool:
            continue
        tag = release.get("tag_name", "")
        if not isinstance(tag, str) or not TAG.fullmatch(tag):
            continue
        try:
            published = datetime.fromisoformat(release["published_at"].replace("Z", "+00:00"))
            if published.tzinfo is None:
                continue
        except (KeyError, ValueError, TypeError, AttributeError):
            continue
        version = tag[1:]
        release_url = f"https://github.com/{repo}/releases/tag/{quote(tag, safe='')}"
        if not same_github_url(release.get("html_url"), release_url):
            continue
        names = (f"emerald-thor-{version}-armeabi-v7a.apk", "SHA256SUMS", "build-info.json")
        assets = {}
        for name in names:
            matches = [a for a in release.get("assets", []) if a.get("name") == name]
            expected_url = f"https://github.com/{repo}/releases/download/{quote(tag, safe='')}/{quote(name, safe='')}"
            if len(matches) != 1:
                break
            asset = matches[0]
            if (asset.get("state") != "uploaded" or type(asset.get("size")) is not int or asset["size"] <= 0
                    or not same_github_url(asset.get("browser_download_url"), expected_url)):
                break
            assets[name] = {"url": expected_url, "bytes": asset["size"]}
        if len(assets) != len(names):
            continue
        complete.append((published, {
            "repository": repo,
            "repository_url": f"https://github.com/{repo}",
            "restricted": repository["private"],
            "tag": tag,
            "version": version,
            "prerelease": release["prerelease"],
            "published_at": published.astimezone(timezone.utc).isoformat().replace("+00:00", "Z"),
            "release_url": release_url,
            "apk_url": assets[names[0]]["url"],
            "apk_bytes": assets[names[0]]["bytes"],
            "checksums_url": assets[names[1]]["url"],
            "manifest_url": assets[names[2]]["url"],
        }))
    if not complete:
        raise SiteError("no published tagged release has a complete ARMv7 APK/checksum/manifest set")
    return max(complete, key=lambda entry: (entry[0], entry[1]["tag"]))[1]


def api(path: str, token: str | None) -> object:
    headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28",
               "User-Agent": "Emerald-Thor-Pages-build"}
    if token:
        headers["Authorization"] = "Bearer " + token
    request = Request("https://api.github.com/" + path, headers=headers)
    try:
        with urlopen(request, timeout=30) as response:
            return json.load(response)
    except HTTPError as exc:
        raise SiteError(f"GitHub metadata request failed (HTTP {exc.code}); use a read token or an explicit offline snapshot") from None
    except (URLError, ValueError) as exc:
        raise SiteError("could not read GitHub release metadata; no download link was guessed") from exc


def fetch_feed(repo: str, token: str | None) -> tuple[dict, list[dict]]:
    repository = api(f"repos/{repo}", token)
    releases = []
    for page in range(1, 101):
        batch = api(f"repos/{repo}/releases?per_page=100&page={page}", token)
        if not isinstance(batch, list):
            raise SiteError("invalid GitHub release response")
        releases.extend(batch)
        if len(batch) < 100:
            break
    else:
        raise SiteError("release pagination limit reached; refusing a possibly stale selection")
    if not isinstance(repository, dict):
        raise SiteError("invalid GitHub repository response")
    return repository, releases


def render(template: str, values: dict[str, str]) -> str:
    def replace(match):
        key = match[1]
        if key not in values:
            raise SiteError("unknown site template field: " + key)
        return html.escape(values[key], quote=True)
    return re.sub(r"\{\{([A-Z_]+)\}\}", replace, template)


def build(output: Path, release: dict, root: Path = ROOT) -> None:
    output = output.absolute()
    if output.is_symlink() or output.resolve() == root.resolve() or (root / "site").resolve().is_relative_to(output.resolve()):
        raise SiteError("output must be a separate generated directory")
    if output.exists() and not (output / ".emerald-site").is_file():
        raise SiteError("refusing to replace an output directory not created by this builder")
    published = datetime.fromisoformat(release["published_at"].replace("Z", "+00:00"))
    values = {
        "RELEASE_TAG": release["tag"], "RELEASE_VERSION": release["version"],
        "RELEASE_KIND": "Android preview" if release["prerelease"] else "Release",
        "RELEASE_DATE": f"{published.day} {published.strftime('%b %Y')}",
        "RELEASE_URL": release["release_url"], "APK_URL": release["apk_url"],
        "APK_SIZE": f"{release['apk_bytes'] / (1024 * 1024):.1f} MiB",
        "CHECKSUMS_URL": release["checksums_url"], "MANIFEST_URL": release["manifest_url"],
        "REPOSITORY_URL": release["repository_url"],
        "ACCESS_LABEL": "Private preview · GitHub access required" if release["restricted"] else "Signed Android download",
        "ACCESS_NOTE": ("Downloads are in a private GitHub repository. Sign in with an account that has repository access."
                        if release["restricted"] else "Download the signed ARMv7 APK. Matching game data is included."),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="emerald-site-", dir=output.parent) as directory:
        stage = Path(directory) / "site"
        stage.mkdir()
        for name in TEMPLATES:
            (stage / name).write_text(render(source_file(root, "site/" + name).read_text(), values), encoding="utf-8")
        for name in STATIC_FILES:
            shutil.copyfile(source_file(root, "site/" + name), stage / name)
        for name, relative in MEDIA.items():
            source = source_file(root, relative)
            target = stage / "assets" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        (stage / "assets/fonts").mkdir(parents=True, exist_ok=True)
        for name in FONT_FILES:
            shutil.copyfile(source_file(root, "site/assets/fonts/" + name), stage / "assets/fonts" / name)
        (stage / "release.json").write_text(json.dumps(release, indent=2) + "\n", encoding="utf-8")
        (stage / ".nojekyll").touch()
        (stage / ".emerald-site").write_text("Generated by tools/build_site.py\n")
        validate_artifact(stage)
        if output.exists():
            shutil.rmtree(output)
        stage.rename(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", DEFAULT_REPOSITORY))
    parser.add_argument("--output", type=Path, default=ROOT / "build/site")
    parser.add_argument("--snapshot", type=Path, help="explicit offline repository/releases JSON feed")
    args = parser.parse_args()
    repo = repository_name(args.repository)
    if args.snapshot:
        feed = json.loads(args.snapshot.read_text())
        repository, releases = feed["repository"], feed["releases"]
    else:
        repository, releases = fetch_feed(repo, os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN"))
    release = select_release(repository, releases, repo)
    build(args.output, release)
    print(f"Built {args.output}: {release['tag']} ({'restricted downloads' if release['restricted'] else 'public downloads'})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (SiteError, OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        raise SystemExit("build_site: " + str(exc)) from None
