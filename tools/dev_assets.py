#!/usr/bin/env python3
"""Copy changed game-data files to the console's loose data folder.

    python tools/dev_assets.py --dest E:\\              # SD card in this PC
    python tools/dev_assets.py --ftp 192.168.1.20:5000  # FTP server on the 3DS

Reads 3ds_port/romfs (the build's staging, after `make`), compares each game
data file with what was sent last time (a hash cache in
3ds_port/build/dev_assets.json) and uploads only the differences to
/3ds/emerald3ds/devdata/, then writes the marker that makes the game use the
loose backend. `--all` resends everything; `--off` removes the marker so the
game goes back to its embedded data or the data pack.
"""

from __future__ import annotations

import argparse
import ftplib
import hashlib
import io
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "port_common"))
import staging  # noqa: E402

REMOTE = "3ds/emerald3ds/devdata"
MARKER = ".emerald3ds-dev"


class LocalTarget:
    def __init__(self, root: Path):
        self.base = Path(root) / REMOTE

    def put(self, rel: str, data: bytes) -> None:
        path = self.base / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def remove(self, rel: str) -> None:
        (self.base / rel).unlink(missing_ok=True)


class FtpTarget:
    def __init__(self, address: str):
        host, _, port = address.partition(":")
        self.ftp = ftplib.FTP()
        self.ftp.connect(host, int(port or 5000), timeout=30)
        self.ftp.login()
        self.made: set[str] = set()

    def _mkdirs(self, folder: str) -> None:
        parts = folder.split("/")
        for i in range(1, len(parts) + 1):
            sub = "/" + "/".join(parts[:i])
            if sub in self.made:
                continue
            try:
                self.ftp.mkd(sub)
            except ftplib.error_perm:
                pass
            self.made.add(sub)

    def put(self, rel: str, data: bytes) -> None:
        remote = "%s/%s" % (REMOTE, rel)
        self._mkdirs(remote.rsplit("/", 1)[0])
        self.ftp.storbinary("STOR /" + remote, io.BytesIO(data))

    def remove(self, rel: str) -> None:
        try:
            self.ftp.delete("/%s/%s" % (REMOTE, rel))
        except ftplib.error_perm:
            pass


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    where = ap.add_mutually_exclusive_group(required=True)
    where.add_argument("--dest", type=Path, help="root of the SD card")
    where.add_argument("--ftp", help="HOST[:PORT] of an FTP server on the console")
    ap.add_argument("--romfs", type=Path, default=ROOT / "3ds_port" / "romfs")
    ap.add_argument("--all", action="store_true", help="send every file again")
    ap.add_argument("--off", action="store_true", help="remove the loose-data marker")
    args = ap.parse_args()

    target = LocalTarget(args.dest) if args.dest else FtpTarget(args.ftp)
    if args.off:
        target.remove(MARKER)
        print("dev_assets: loose data disabled")
        return
    cache_path = ROOT / "3ds_port" / "build" / "dev_assets.json"
    key = str(args.dest or args.ftp)
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    sent = {} if args.all else cache.get(key, {})
    abi, _ = staging.compute_abi(args.romfs)
    count = size = 0
    for rel, path in staging.data_files(args.romfs):
        data = path.read_bytes()
        digest = hashlib.sha1(data).hexdigest()
        if sent.get(rel) == digest:
            continue
        target.put(rel, data)
        sent[rel] = digest
        count += 1
        size += len(data)
    target.put(MARKER, ("abi %08x\n" % abi).encode("ascii"))
    cache[key] = sent
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_text(json.dumps(cache))
    print("dev_assets: %d files (%.1f MiB) sent, ABI %08x" % (count, size / 1048576, abi))


if __name__ == "__main__":
    main()
