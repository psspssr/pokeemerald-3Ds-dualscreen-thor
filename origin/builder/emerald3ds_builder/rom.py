"""Recognise the player's ROM.

Only one ROM is supported: Pokémon Emerald (USA, Europe), 16 MiB,
SHA-1 f3ae088181bf583e55daf962a92bb46f4f1d07b7. A trimmed dump (trailing 0xFF
removed) is padded back before it is checked; a .zip holding a single .gba is
opened directly. The ROM is only ever read into memory: it is never copied,
written or sent anywhere.
"""

from __future__ import annotations

import hashlib
import zipfile
from dataclasses import dataclass
from pathlib import Path

from .errors import BuilderError

SUPPORTED_SHA1 = "f3ae088181bf583e55daf962a92bb46f4f1d07b7"
ROM_SIZE = 16 * 1024 * 1024
KNOWN_CODES = {
    "BPEE": "Pokemon Emerald (USA, Europe)",
    "BPEJ": "Pokemon Emerald (Japan)",
    "BPES": "Pokemon Emerald (Spain)",
    "BPED": "Pokemon Emerald (Germany)",
    "BPEF": "Pokemon Emerald (France)",
    "BPEI": "Pokemon Emerald (Italy)",
    "AXVE": "Pokemon Ruby",
    "AXPE": "Pokemon Sapphire",
    "BPRE": "Pokemon FireRed",
    "BPGE": "Pokemon LeafGreen",
}


@dataclass
class Rom:
    data: bytes
    sha1: str
    title: str
    code: str
    source: Path


def _read(path: Path) -> bytes:
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as zf:
            names = [n for n in zf.namelist() if n.lower().endswith((".gba", ".agb", ".bin"))]
            if len(names) != 1:
                raise BuilderError("The ZIP file must contain exactly one .gba file.")
            return zf.read(names[0])
    return path.read_bytes()


def header(data: bytes) -> tuple[str, str]:
    if len(data) < 0xC0:
        return "", ""
    title = data[0xA0:0xAC].split(b"\0")[0].decode("ascii", "replace")
    code = data[0xAC:0xB0].decode("ascii", "replace")
    return title, code


def load_rom(path: Path) -> Rom:
    path = Path(path)
    if not path.is_file():
        raise BuilderError("The ROM file was not found:\n%s" % path.name)
    try:
        data = _read(path)
    except (OSError, zipfile.BadZipFile) as exc:
        raise BuilderError("The ROM file could not be read.", str(exc)) from exc
    if len(data) > ROM_SIZE:
        raise BuilderError("This file is larger than a GBA cartridge; it is not the supported ROM.")
    if len(data) < ROM_SIZE:
        # A trimmed dump: the cartridge's trailing 0xFF fill was cut off.
        data = data + b"\xff" * (ROM_SIZE - len(data))
    title, code = header(data)
    sha1 = hashlib.sha1(data).hexdigest()
    if sha1 != SUPPORTED_SHA1:
        what = KNOWN_CODES.get(code)
        if what and code != "BPEE":
            raise BuilderError("This ROM is %s. Only Pokemon Emerald (USA, Europe) is supported." % what)
        if code == "BPEE":
            raise BuilderError(
                "This is a Pokemon Emerald (USA, Europe) ROM, but not an unmodified one.",
                "Patched, hacked or bad dumps are not supported. Use a clean dump of your cartridge "
                "(SHA-1 %s)." % SUPPORTED_SHA1)
        raise BuilderError("This file is not the supported ROM.",
                           "Expected Pokemon Emerald (USA, Europe), SHA-1 %s." % SUPPORTED_SHA1)
    return Rom(data=data, sha1=sha1, title=title, code=code, source=path)
