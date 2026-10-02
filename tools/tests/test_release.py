"""Release identity, package and retry tests: no network or signing secrets."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import release

SHA = "a" * 40
OTHER = "b" * 40
TAG = "v0.1.0-alpha.2"
REF = "refs/tags/" + TAG


class FakeGitHub:
    def __init__(self):
        self.repo = {"private": True, "default_branch": "main"}
        self.release = {"id": 27, "tag_name": TAG, "target_commitish": SHA,
                        "draft": False, "prerelease": True, "published_at": "2026-10-02T12:00:00Z"}
        self.tag = {"type": "commit", "sha": SHA}
        self.annotated = {}
        self.main = {"type": "commit", "sha": SHA}
        self.comparison = "identical"
        self.data = {}
        self.uploads = []
        self.fail_upload = None
        self.after_upload = None

    def assets(self):
        return [{"id": index + 1, "name": name, "size": len(data)}
                for index, (name, data) in enumerate(self.data.items())]

    def get(self, endpoint):
        prefix = "repos/" + release.REPOSITORY
        if endpoint == prefix:
            return copy.deepcopy(self.repo)
        if endpoint == prefix + "/releases/27":
            return {**copy.deepcopy(self.release), "assets": self.assets()}
        if endpoint == prefix + "/git/ref/tags/" + TAG:
            return {"object": copy.deepcopy(self.tag)}
        if endpoint.startswith(prefix + "/git/tags/"):
            return {"object": copy.deepcopy(self.annotated[endpoint.rsplit("/", 1)[-1]])}
        if endpoint == prefix + "/git/ref/heads/main":
            return {"object": copy.deepcopy(self.main)}
        if endpoint.startswith(prefix + "/compare/"):
            return {"status": self.comparison}
        raise AssertionError("unexpected API read: " + endpoint)

    def download(self, asset, target):
        current = self.assets()[asset["id"] - 1]
        self.assert_name = current["name"]
        target.write_bytes(self.data[current["name"]])

    def upload(self, identifier, name, path):
        if name == self.fail_upload:
            raise release.ReleaseError("simulated upload failure")
        assert identifier == 27 and name not in self.data
        self.data[name] = path.read_bytes()
        self.uploads.append((identifier, name))
        if self.after_upload:
            self.after_upload(self)
        return self.assets()[-1]


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.api = FakeGitHub()
        self.event = {"action": "published", "repository": {"full_name": release.REPOSITORY, "private": True},
                      "release": copy.deepcopy(self.api.release)}
        self.plan, _ = release.context(self.api, self.event, "1", SHA, REF)
        self.temporary = tempfile.TemporaryDirectory(prefix="emerald-release-test-")
        self.addCleanup(self.temporary.cleanup)
        self.bundle = Path(self.temporary.name)
        apk = self.bundle / self.plan["apk"]
        checks = {}
        with zipfile.ZipFile(apk, "w") as archive:
            for name in release.CORE_FILES:
                data = ("test fixture " + name).encode()
                archive.writestr(name, data)
                checks[name] = release.sha256(data)
        self.info = {**self.plan, "application_id": release.APPLICATION_ID,
                     "debuggable": False, "abi": ["armeabi-v7a"], "min_sdk": 28, "target_sdk": 35,
                     "signer_sha256": release.SIGNER_SHA256, "packaged_sha256": checks,
                     "size": apk.stat().st_size, "sha256": release.file_hash(apk)}
        self.write_manifest()

    def write_manifest(self):
        (self.bundle / "build-info.json").write_text(json.dumps(self.info, sort_keys=True) + "\n")
        (self.bundle / "SHA256SUMS").write_text(release.checksum_text(self.bundle, self.plan))

    def preflight(self):
        return release.preflight(self.api, self.event, "1", SHA, REF)

    def publish(self):
        release.publish(self.api, self.event, "1", SHA, REF, self.bundle)

    def test_version_from_tag_and_run_number_is_stable_and_monotonic(self):
        self.assertEqual(("0.1.0-alpha.2", 2), release.versions(TAG, "1"))
        self.assertEqual(release.versions(TAG, "17"), release.versions(TAG, "17"))
        self.assertEqual(("2.0.0-rc.1+test.007", 19), release.versions("v2.0.0-rc.1+test.007", "18"))
        self.assertLess(release.versions(TAG, "17")[1], release.versions("v1.0.0", "18")[1])

    def test_unsafe_tags_non_semver_codes_and_android_overflow_are_rejected(self):
        for tag in ("main", "0.1.0", "v01.2.3", "v1.2.3-01", "v1.2.3;echo", "v1.2.3\n", "v1.2.3/a"):
            with self.subTest(tag=tag), self.assertRaises(release.ReleaseError):
                release.versions(tag, "1")
        for number in ("0", "-1", "1\n", "1.0", "2100000000"):
            with self.subTest(number=number), self.assertRaises(release.ReleaseError):
                release.versions(TAG, number)

    def test_published_private_tag_context_is_required(self):
        for change in (lambda: self.event.update(action="edited"),
                       lambda: self.event["repository"].update(full_name="other/repo"),
                       lambda: self.api.repo.update(private=False),
                       lambda: self.api.repo.update(default_branch="other"),
                       lambda: self.api.release.update(draft=True),
                       lambda: self.api.release.update(tag_name="v2.0.0"),
                       lambda: self.api.release.update(id=28),
                       lambda: self.api.release.update(target_commitish=OTHER)):
            self.api = FakeGitHub()
            self.event = {"action": "published", "repository": {"full_name": release.REPOSITORY, "private": True},
                          "release": copy.deepcopy(self.api.release)}
            change()
            with self.assertRaises(release.ReleaseError):
                self.preflight()
            self.assertFalse(self.api.uploads)

    def test_tag_movement_branch_ambiguity_and_nonmain_history_are_rejected(self):
        self.api.tag["sha"] = OTHER
        with self.assertRaisesRegex(release.ReleaseError, "tag moved"):
            self.preflight()
        self.api.tag["sha"] = SHA
        self.api.release["target_commitish"] = self.event["release"]["target_commitish"] = "feature"
        with self.assertRaisesRegex(release.ReleaseError, "target must"):
            self.preflight()
        self.api.release["target_commitish"] = self.event["release"]["target_commitish"] = "main"
        self.api.comparison = "diverged"
        with self.assertRaisesRegex(release.ReleaseError, "main's history"):
            self.preflight()
        with self.assertRaisesRegex(release.ReleaseError, "workflow ref"):
            release.context(self.api, self.event, "1", SHA, "refs/heads/" + TAG)

    def test_annotated_tag_peels_to_the_commit_and_cycles_fail(self):
        self.api.tag = {"type": "tag", "sha": OTHER}
        self.api.annotated[OTHER] = {"type": "commit", "sha": SHA}
        self.assertEqual(SHA, self.preflight()["code_commit"])
        self.api.annotated[OTHER] = {"type": "tag", "sha": OTHER}
        with self.assertRaisesRegex(release.ReleaseError, "cyclic"):
            self.preflight()

    def test_fresh_publish_and_complete_rerun_are_exactly_idempotent(self):
        self.assertEqual("build", self.preflight()["mode"])
        self.publish()
        original = self.api.data.copy()
        self.assertEqual(3, len(self.api.uploads))
        self.assertEqual("existing", self.preflight()["mode"])
        self.publish()
        self.assertEqual(original, self.api.data)
        self.assertEqual(3, len(self.api.uploads))

    def test_existing_version_code_source_or_signer_mismatch_fails_closed(self):
        for key, value in (("version_code", 3), ("version", "9.0.0"), ("code_commit", OTHER),
                           ("release_id", 28), ("signer_sha256", "f" * 64)):
            with self.subTest(key=key):
                old = self.info[key]
                self.info[key] = value
                self.write_manifest()
                self.api.data = {name: (self.bundle / name).read_bytes() for name in release.asset_names(self.plan)}
                with self.assertRaises(release.ReleaseError):
                    self.preflight()
                self.assertFalse(self.api.uploads)
                self.info[key] = old

    def test_partial_upload_retry_reuses_exact_bytes_without_replacement(self):
        self.api.fail_upload = self.plan["apk"]
        with self.assertRaisesRegex(release.ReleaseError, "simulated"):
            self.publish()
        manifest = self.api.data["build-info.json"]
        self.assertEqual(["build-info.json"], list(self.api.data))
        with self.assertRaisesRegex(release.ReleaseError, "re-run the failed publish job"):
            self.preflight()
        self.api.fail_upload = None
        self.publish()
        self.assertEqual(manifest, self.api.data["build-info.json"])
        self.assertEqual(3, len(self.api.uploads))

    def test_conflicting_existing_apk_causes_zero_uploads(self):
        self.api.data[self.plan["apk"]] = b"previous published bytes"
        with self.assertRaisesRegex(release.ReleaseError, "refusing to overwrite"):
            self.publish()
        self.assertFalse(self.api.uploads)
        self.assertEqual(b"previous published bytes", self.api.data[self.plan["apk"]])

    def test_tag_is_rechecked_between_individual_uploads(self):
        self.api.after_upload = lambda api: api.tag.update(sha=OTHER)
        with self.assertRaisesRegex(release.ReleaseError, "tag moved"):
            self.publish()
        self.assertEqual(1, len(self.api.uploads))
        self.assertNotIn(self.plan["apk"], self.api.data)

    def test_bundle_rejects_bytes_checksum_and_unexpected_file_changes(self):
        release.verify_bundle(self.bundle, self.plan)
        checksum = self.bundle / "SHA256SUMS"
        checksum.write_text("wrong\n")
        with self.assertRaisesRegex(release.ReleaseError, "checksums differ"):
            release.verify_bundle(self.bundle, self.plan)
        self.write_manifest()
        extra = self.bundle / "release.keystore"
        extra.write_text("must never be included")
        with self.assertRaisesRegex(release.ReleaseError, "only APK"):
            release.verify_bundle(self.bundle, self.plan)
        extra.unlink()
        (self.bundle / self.plan["apk"]).write_bytes(b"changed")
        with self.assertRaisesRegex(release.ReleaseError, "APK does not match"):
            release.verify_bundle(self.bundle, self.plan)

    def test_core_native_bytes_are_checked_beyond_outer_apk_checksum(self):
        self.info["packaged_sha256"][release.CORE_FILES[0]] = "0" * 64
        self.write_manifest()
        with self.assertRaisesRegex(release.ReleaseError, "packaged bytes differ"):
            release.verify_bundle(self.bundle, self.plan)

    def test_real_package_inspection_rejects_debug_abi_version_and_wrong_signer(self):
        valid = ("package: name='com.emerald3ds.android' versionCode='2' versionName='0.1.0-alpha.2'\n"
                 "sdkVersion:'28'\ntargetSdkVersion:'35'\nnative-code: 'armeabi-v7a'\n")
        for badging in (valid + "application-debuggable\n", valid.replace("armeabi-v7a", "x86_64"),
                        valid.replace("versionCode='2'", "versionCode='1'")):
            with patch.object(release.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, badging, "")):
                with self.assertRaises(release.ReleaseError):
                    release.inspect_apk(self.bundle / self.plan["apk"], self.plan, Path("/sdk"), signed=False)
        with patch.object(release.subprocess, "run", side_effect=[
                subprocess.CompletedProcess([], 0, valid, ""),
                subprocess.CompletedProcess([], 0, "Signer #1 certificate SHA-256 digest: " + "0" * 64, "")]):
            with self.assertRaisesRegex(release.ReleaseError, "signer differs"):
                release.inspect_apk(self.bundle / self.plan["apk"], self.plan, Path("/sdk"))


if __name__ == "__main__":
    unittest.main()
