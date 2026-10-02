#!/usr/bin/env python3
"""Turn the position-independent executable GNU ld wrote into a library.

    elf_dyn.py FILE

The library is linked with -pie rather than -shared so that --gc-sections
works as in origin's executable link: in a -shared link every global symbol
is exported and therefore a garbage-collection root, so the dead GBA-only
code origin relies on the linker to drop (and its references to the excluded
Mystery Gift and e-Reader units) would stay. A -pie link keeps globals out of
the dynamic symbol table, exports only --export-dynamic-symbol names, and
leaves .symtab bindings as in origin's ELF, which origin's scripts read.

Two header fields differ from a shared object, and only these are changed:
GNU ld writes e_type ET_EXEC for a PIE whose first segment is not at 0
(ours is at the load address), and sets DF_1_PIE. Code, data, dynamic
relocations and layout are those of a shared object linked at that address.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ET_EXEC, ET_DYN = 2, 3
PT_DYNAMIC = 2
DT_NULL, DT_FLAGS_1 = 0, 0x6FFFFFFB
DF_1_PIE = 0x08000000


def main() -> int:
    path = Path(sys.argv[1])
    data = bytearray(path.read_bytes())
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise SystemExit("elf_dyn: %s is not a 32-bit little-endian ELF" % path)
    e_type, = struct.unpack_from("<H", data, 16)
    if e_type not in (ET_EXEC, ET_DYN):
        raise SystemExit("elf_dyn: %s has e_type %d" % (path, e_type))
    struct.pack_into("<H", data, 16, ET_DYN)

    e_phoff, = struct.unpack_from("<I", data, 28)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 42)
    for i in range(e_phnum):
        p_type, p_offset, _, _, p_filesz = struct.unpack_from("<IIIII", data, e_phoff + i * e_phentsize)
        if p_type != PT_DYNAMIC:
            continue
        for off in range(p_offset, p_offset + p_filesz, 8):
            tag, val = struct.unpack_from("<iI", data, off)
            if tag == DT_NULL:
                break
            if tag == DT_FLAGS_1:
                struct.pack_into("<I", data, off + 4, val & ~DF_1_PIE)
    path.write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
