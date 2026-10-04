"""Static-site release selection and publication-boundary regressions."""
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import quote, urlsplit

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import build_site
import publish_site


class SiteTests(unittest.TestCase):
    repo = build_site.DEFAULT_REPOSITORY
    repository = {"full_name": repo, "private": True}

    def release(self, tag="v0.1.0-alpha.7", date="2026-10-04T09:47:36Z", prerelease=True):
        prefix = f"https://github.com/{self.repo}"
        return {"tag_name": tag, "published_at": date, "draft": False, "prerelease": prerelease,
                "html_url": f"{prefix}/releases/tag/{quote(tag, safe='')}",
                "assets": [{"name": name, "state": "uploaded", "size": size,
                            "browser_download_url": f"{prefix}/releases/download/{quote(tag, safe='')}/{quote(name, safe='')}"}
                           for name, size in ((f"emerald-thor-{tag[1:]}-armeabi-v7a.apk", 123456),
                                              ("SHA256SUMS", 191), ("build-info.json", 1799))]}

    def select(self, *releases):
        return build_site.select_release(self.repository, list(releases), self.repo)

    def source_fixture(self, folder):
        root = Path(folder) / "source"
        shutil.copytree(ROOT / "site", root / "site")
        for relative in build_site.MEDIA.values():
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        shutil.copyfile(ROOT / "LICENSE", root / "LICENSE")
        return root

    def assert_publisher_rejects(self, root):
        # Invalid artifacts must fail before credentials or network/git access.
        # No real deploy key is available to this test, even in a developer shell.
        with patch.object(publish_site, "ROOT", root), patch.dict(os.environ, {}, clear=True), \
             patch.object(publish_site, "urlopen") as network, \
             patch.object(publish_site.subprocess, "run") as git:
            with self.assertRaisesRegex(build_site.SiteError, "website artifact"):
                publish_site.publish()
            network.assert_not_called()
            git.assert_not_called()

    def test_newest_complete_publication_includes_prereleases(self):
        stable = self.release("v1.0.0", "2026-10-01T00:00:00Z", False)
        preview = self.release()
        self.assertEqual(preview["tag_name"], self.select(stable, preview)["tag"])
        self.assertEqual(preview["tag_name"], self.select(preview, stable)["tag"])

    def test_draft_and_partially_uploaded_newer_release_do_not_replace_download(self):
        good = self.release()
        for invalid in ("draft", "missing", "empty", "uploading", "duplicate", "wrong_apk"):
            newer = self.release("v0.1.0-alpha.8", "2026-10-05T00:00:00Z")
            if invalid == "draft": newer["draft"] = True
            if invalid == "missing": newer["assets"].pop()
            if invalid == "empty": newer["assets"][0]["size"] = 0
            if invalid == "uploading": newer["assets"][0]["state"] = "starter"
            if invalid == "duplicate": newer["assets"].append(newer["assets"][0].copy())
            if invalid == "wrong_apk": newer["assets"][0]["name"] = "app-debug.apk"
            with self.subTest(invalid=invalid):
                self.assertEqual(good["tag_name"], self.select(newer, good)["tag"])

    def test_foreign_or_secret_bearing_urls_and_unsafe_tags_fail_closed(self):
        for value in ("javascript:alert(1)", "https://elsewhere.invalid/game.apk",
                      "https://github.com/wrong/repo/releases/download/v0.1.0-alpha.7/app.apk",
                      self.release()["assets"][0]["browser_download_url"] + "?token=never-publish"):
            bad = self.release()
            bad["assets"][0]["browser_download_url"] = value
            with self.subTest(url=value), self.assertRaises(build_site.SiteError): self.select(bad)
        bad = self.release(); bad["tag_name"] = '<img src=x onerror="bad">'
        with self.assertRaises(build_site.SiteError): self.select(bad)
        with self.assertRaises(build_site.SiteError):
            build_site.select_release({"full_name": "other/repo", "private": False}, [self.release()], self.repo)

    def test_no_release_does_not_invent_latest_endpoint(self):
        with self.assertRaisesRegex(build_site.SiteError, "no published tagged release"):
            self.select()

    def test_live_feed_paginates_without_using_latest_endpoint(self):
        first = [self.release("v0.1.0-alpha.6", "2026-10-03T00:00:00Z") for _ in range(100)]
        latest = self.release()
        with patch.object(build_site, "api", side_effect=[self.repository, first, [latest]]) as request:
            repository, releases = build_site.fetch_feed(self.repo, "test-token-only")
        self.assertEqual(latest["tag_name"], build_site.select_release(repository, releases, self.repo)["tag"])
        self.assertEqual([f"repos/{self.repo}", f"repos/{self.repo}/releases?per_page=100&page=1",
                          f"repos/{self.repo}/releases?per_page=100&page=2"],
                         [call.args[0] for call in request.call_args_list])

    def test_published_output_contains_only_allowlisted_metadata_and_assets(self):
        raw = self.release()
        raw.update({"body": "DO_NOT_PUBLISH_PRIVATE_RELEASE_BODY", "token": "DO_NOT_PUBLISH_TOKEN"})
        selected = self.select(raw)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "site"
            build_site.build(output, selected)
            document = (output / "index.html").read_text()
            metadata = (output / "release.json").read_text()
            self.assertIn("GitHub access required", document)
            self.assertIn(selected["apk_url"], document)
            self.assertNotIn("/releases/latest", document)
            self.assertNotIn("{{", document)
            self.assertNotIn("DO_NOT_PUBLISH", document + metadata)
            self.assertEqual(selected, json.loads(metadata))
            self.assertFalse(any(p.suffix in (".apk", ".so", ".pak") for p in output.rglob("*")))
            self.assertFalse((output / "release-snapshot.json").exists())
            for name, source in build_site.MEDIA.items():
                self.assertEqual((ROOT / source).read_bytes(), (output / "assets" / name).read_bytes())
            for name in ("pixelify-sans.ttf", "OFL.txt", "SOURCE.txt"):
                self.assertEqual((ROOT / "site/assets/fonts" / name).read_bytes(),
                                 (output / "assets/fonts" / name).read_bytes())
            self.assertEqual(build_site.ARTIFACT_FILES,
                             {p.relative_to(output).as_posix() for p in output.rglob("*") if p.is_file()})
            previous = {p.relative_to(output): p.read_bytes() for p in output.rglob("*") if p.is_file()}
            # Ordinary rebuilds replace only the builder's marked output.
            build_site.build(output, selected)
            self.assertEqual(previous,
                             {p.relative_to(output): p.read_bytes() for p in output.rglob("*") if p.is_file()})

    def test_font_copy_ignores_unlisted_payloads_and_external_links(self):
        with tempfile.TemporaryDirectory() as folder:
            root = self.source_fixture(folder)
            outside = Path(folder) / "private-canary.key"
            outside.write_bytes(b"TEST_ONLY_OUTSIDE_CANARY")
            fonts = root / "site/assets/fonts"
            (fonts / "unintended.sav").write_bytes(b"TEST_ONLY_UNRELATED_SAVE")
            (fonts / "nested").mkdir()
            (fonts / "nested/private.key").symlink_to(outside)
            output = Path(folder) / "generated"
            build_site.build(output, self.select(self.release()), root=root)
            self.assertEqual({"pixelify-sans.ttf", "OFL.txt", "SOURCE.txt"},
                             {p.name for p in (output / "assets/fonts").iterdir()})
            for path in output.rglob("*"):
                if path.is_file():
                    self.assertNotIn(b"TEST_ONLY_OUTSIDE_CANARY", path.read_bytes())
                    self.assertNotIn(b"TEST_ONLY_UNRELATED_SAVE", path.read_bytes())
            self.assertEqual(b"TEST_ONLY_OUTSIDE_CANARY", outside.read_bytes())

    def test_allowlisted_source_links_are_rejected_before_replacing_output(self):
        cases = ("external_font", "internal_font", "font_directory", "template", "script", "image")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as folder:
                root = self.source_fixture(folder)
                output = Path(folder) / "generated"
                build_site.build(output, self.select(self.release()), root=root)
                previous = (output / "index.html").read_bytes()
                outside = Path(folder) / "private-canary.key"
                outside.write_bytes(b"TEST_ONLY_OUTSIDE_CANARY")
                paths = {"external_font": "site/assets/fonts/pixelify-sans.ttf",
                         "internal_font": "site/assets/fonts/pixelify-sans.ttf",
                         "font_directory": "site/assets/fonts", "template": "site/index.html",
                         "script": "site/site.js", "image": "docs/images/classic-town.png"}
                source = root / paths[case]
                if case == "font_directory":
                    moved = Path(folder) / "outside-fonts"
                    source.rename(moved)
                    source.symlink_to(moved, target_is_directory=True)
                else:
                    source.unlink()
                    target = root / "site/assets/fonts/OFL.txt" if case == "internal_font" else outside
                    source.symlink_to(target)
                with self.assertRaisesRegex(build_site.SiteError, "symbolic links"):
                    build_site.build(output, self.select(self.release()), root=root)
                self.assertEqual(previous, (output / "index.html").read_bytes())
                self.assertEqual(b"TEST_ONLY_OUTSIDE_CANARY", outside.read_bytes())

    def test_source_path_escapes_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = self.source_fixture(folder)
            outside = Path(folder) / "private-canary.key"
            outside.write_bytes(b"TEST_ONLY_OUTSIDE_CANARY")
            for escaped in ("../private-canary.key", str(outside)):
                with self.subTest(path=escaped), patch.object(build_site, "MEDIA", {"classic-town.png": escaped}):
                    with self.assertRaisesRegex(build_site.SiteError, "inside the source tree"):
                        build_site.build(Path(folder) / "generated", self.select(self.release()), root=root)
                    self.assertFalse((Path(folder) / "generated").exists())

    def test_publisher_rejects_unexpected_nested_files_and_directories(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / "build/site"
            build_site.build(output, self.select(self.release()))
            for relative in ("assets/backup.sav", "assets/fonts/private.key", "assets/fonts/app.apk"):
                with self.subTest(path=relative):
                    extra = output / relative
                    extra.write_bytes(b"TEST_ONLY_UNEXPECTED_PAYLOAD")
                    self.assert_publisher_rejects(root)
                    self.assertEqual(b"TEST_ONLY_UNEXPECTED_PAYLOAD", extra.read_bytes())
                    extra.unlink()
            (output / "assets/unexpected").mkdir()
            self.assert_publisher_rejects(root)

    def test_publisher_rejects_symlink_files_directories_and_artifact_root(self):
        for relative in ("assets/fonts/OFL.txt", "assets/fonts", "."):
            with self.subTest(path=relative), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                output = root / "build/site"
                build_site.build(output, self.select(self.release()))
                source = output / relative
                outside = root / "outside-target"
                source.rename(outside)
                source.symlink_to(outside, target_is_directory=outside.is_dir())
                self.assert_publisher_rejects(root)
                self.assertTrue(outside.exists())

    def test_publisher_rejects_missing_or_nonregular_expected_file(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / "build/site"
            build_site.build(output, self.select(self.release()))
            font = output / "assets/fonts/OFL.txt"
            font.unlink()
            self.assert_publisher_rejects(root)
            font.mkdir()
            self.assert_publisher_rejects(root)

    def test_public_access_copy_and_local_links_work_below_a_project_path(self):
        selected = build_site.select_release({"full_name": self.repo, "private": False}, [self.release()], self.repo)
        class Links(HTMLParser):
            def __init__(self): super().__init__(); self.links = []; self.ids = set()
            def handle_starttag(self, tag, attributes):
                values = dict(attributes)
                self.links.extend(values[key] for key in ("href", "src") if key in values)
                if "id" in values: self.ids.add(values["id"])
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "project"
            build_site.build(output, selected)
            for page in ("index.html", "credits.html"):
                text = (output / page).read_text()
                self.assertNotIn("GitHub access required", text)
                links = Links(); links.feed(text)
                for link in links.links:
                    url = urlsplit(link)
                    if url.scheme: continue
                    self.assertFalse(url.path.startswith("/"), link)
                    self.assertTrue((output / (url.path or page)).exists(), link)
                    if not url.path and url.fragment: self.assertIn(url.fragment, links.ids)

    def test_unowned_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "site"; output.mkdir()
            (output / "keep.txt").write_text("unrelated")
            with self.assertRaises(build_site.SiteError): build_site.build(output, self.select(self.release()))
            self.assertEqual("unrelated", (output / "keep.txt").read_text())

    def test_failed_build_keeps_previous_generated_output(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "site"
            build_site.build(output, self.select(self.release()))
            previous = (output / "index.html").read_bytes()
            with patch.object(build_site, "MEDIA", {"missing.png": "site/not-present.png"}), self.assertRaises(build_site.SiteError):
                build_site.build(output, self.select(self.release()))
            self.assertEqual(previous, (output / "index.html").read_bytes())


if __name__ == "__main__":
    unittest.main()
