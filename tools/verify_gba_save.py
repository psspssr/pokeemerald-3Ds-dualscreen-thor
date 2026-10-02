#!/usr/bin/env python3
"""Validate an unwrapped Emerald GBA flash save without changing it.

Checks the standard 14-sector rotating slots, footer signatures/counters,
section checksums, fixed trainer/world/storage sizes and encrypted Pokemon
checksums. A 64 KiB file is treated as a truncated flash image (missing bytes
erased); normal exports are 128 KiB. A valid backup slot permits recovery
from an interrupted newer save. This does not replace a real GBA load test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

SECTOR_SIZE = 4096
DATA_SIZES = (0xF2C, 0xF80, 0xF80, 0xF80, 0xF08,
              0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0x7D0)
SIGNATURE = 0x08012025


def checksum(data: bytes) -> int:
    total = sum(struct.unpack('<' + 'I' * (len(data) // 4), data)) & 0xFFFFFFFF
    return ((total & 0xFFFF) + (total >> 16)) & 0xFFFF


def pokemon_check(mon: bytes, label: str) -> None:
    if not mon[19] & 2:  # Empty box slot.
        return
    personality, trainer = struct.unpack_from('<II', mon)
    key = personality ^ trainer
    decrypted = b''.join(struct.pack('<I', value ^ key)
                         for value in struct.unpack_from('<12I', mon, 32))
    expected, = struct.unpack_from('<H', mon, 28)
    actual = sum(struct.unpack('<24H', decrypted)) & 0xFFFF
    if actual != expected:
        raise ValueError(f'{label}: encrypted Pokemon checksum mismatch')


def verify(data: bytes) -> dict:
    if len(data) not in (65536, 131072):
        raise ValueError('expected a raw 64 KiB or 128 KiB .sav, without emulator save-state headers')
    full = data.ljust(131072, b'\xff')
    slots, valid = [], []
    for slot in range(2):
        parts = {}
        counters = set()
        problems = []
        for physical in range(slot * 14, slot * 14 + 14):
            raw = full[physical * SECTOR_SIZE:(physical + 1) * SECTOR_SIZE]
            section, expected, signature, counter = struct.unpack_from('<HHII', raw, 0xFF4)
            if signature != SIGNATURE:
                problems.append(f'sector {physical}: erased or uncommitted signature')
                continue
            if section >= 14 or section in parts:
                problems.append(f'sector {physical}: invalid or duplicate section {section}')
                continue
            payload = raw[:DATA_SIZES[section]]
            if checksum(payload) != expected:
                problems.append(f'sector {physical}: section {section} checksum mismatch')
                continue
            parts[section] = payload
            counters.add(counter)
        complete = not problems and len(parts) == 14 and len(counters) == 1
        slots.append({'slot': slot, 'valid': complete, 'sections': len(parts),
                      'counters': sorted(counters), 'problems': problems})
        if complete:
            valid.append((slot, next(iter(counters)), parts))
    if not valid:
        raise ValueError('no complete valid Emerald save slot: ' + json.dumps(slots))
    selected = valid[0]
    for candidate in valid[1:]:
        delta = (candidate[1] - selected[1]) & 0xFFFFFFFF
        if 0 < delta < 0x80000000:
            selected = candidate
    slot, counter, parts = selected
    trainer = parts[0]
    world = b''.join(parts[i] for i in range(1, 5))
    storage = b''.join(parts[i] for i in range(5, 14))
    party_count = world[0x234]
    if party_count > 6 or trainer[8] > 1 or storage[0] >= 14:
        raise ValueError('invalid player gender, party count or current PC box')
    for index in range(party_count):
        pokemon_check(world[0x238 + index * 100:0x238 + (index + 1) * 100], f'party {index}')
    for index in range(14 * 30):
        pokemon_check(storage[4 + index * 80:4 + (index + 1) * 80], f'box slot {index}')
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'slots': slots,
            'selected_slot': slot, 'save_counter': counter,
            'player_name_hex': trainer[:8].hex(), 'gender': trainer[8],
            'trainer_id': int.from_bytes(trainer[10:14], 'little'), 'party_count': party_count,
            'position': list(struct.unpack_from('<hh', world)),
            'map_group': world[4], 'map_number': world[5],
            'play_time': [int.from_bytes(trainer[14:16], 'little'), trainer[16], trainer[17]]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('save', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.save.read_bytes()), indent=2))
    except (OSError, ValueError) as error:
        print(f'verify_gba_save: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
