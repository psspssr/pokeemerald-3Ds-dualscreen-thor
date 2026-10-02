#!/usr/bin/env python3
"""Compile real generated Pokemon/RNG units with the production Android rules.

Run tools/bootstrap.py first; its generated sources must include020 hooks.
Host-only fixtures replace the Android prompt, current map and localized
names. Creation, secure Pokemon encryption, stats, moves and RNG are real.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
CC = os.environ.get("CC", "cc")
flags = ["-D_GNU_SOURCE", "-DPORTABLE", "-DMODERN=1", "-DPORT_BRIDGE", "-DPLATFORM_3DS",
         "-I" + str(ROOT / "android/native/include"), "-I" + str(ROOT / "android/host/include"),
         "-iquote" + str(TREE / "include"), "-iquote" + str(TREE / "3ds_port/compat"),
         "-iquote" + str(TREE)]
if "CtrQol_WildPersonality" not in (TREE / "src/pokemon.c").read_text():
    raise SystemExit("run tools/bootstrap.py first to apply the Android QoL hooks")


def function(source, signature):
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}\n", start) + 3]


with tempfile.TemporaryDirectory(prefix="emerald-qol-test-") as directory:
    work = Path(directory)
    pre = subprocess.run([CC, "-E", "-D__INTELLISENSE__", *flags, "src/pokemon.c"],
                         cwd=TREE, check=True, capture_output=True).stdout
    converted = subprocess.run([str(TREE / "tools/preproc/preproc"), "-i", "src/pokemon.c", "charmap.txt"],
                               cwd=TREE, input=pre, check=True, capture_output=True).stdout
    source = work / "pokemon.c"
    source.write_bytes(converted)
    common = ["-std=gnu11", "-O1", "-g", "-ffunction-sections", "-fdata-sections",
              "-fno-omit-frame-pointer", "-fsanitize=address,undefined"]
    # The unchanged GBA setters assemble u32 values by signed promoted-byte
    # shifts. Suppress only that known upstream idiom in its translation
    # unit; new Android rules/bridge and all tests get full ASan + UBSan.
    compiler = subprocess.run([CC, "--version"], check=True, capture_output=True, text=True).stdout
    # Keep unrelated sprite callback tables eligible for section collection;
    # ASan's global registry would otherwise retain the whole game. Stack and
    # heap accesses in the Pokemon routines remain instrumented.
    globals_flag = ["-mllvm", "-asan-globals=0"] if "clang" in compiler.lower() else ["--param=asan-globals=0"]
    subprocess.run([CC, *common, *globals_flag, "-fno-sanitize=shift", "-Wno-pointer-to-int-cast",
                    "-Wno-int-to-pointer-cast", "-Wno-attributes", "-Wno-attribute-alias",
                    "-c", str(source), "-o", str(work / "pokemon.o")], check=True)
    subprocess.run([CC, *common, "-Wall", "-Wextra", "-Werror", *flags,
                    str(ROOT / "android/native/test/test_qol.c"),
                    str(ROOT / "android/native/src/qol_game.c"),
                    str(ROOT / "android/native/src/qol_rules.c"),
                    str(TREE / "src/random.c"), str(work / "pokemon.o"),
                    "-Wl,--gc-sections", "-o", str(work / "test_qol")], check=True)
    subprocess.run([str(work / "test_qol")], check=True, timeout=20,
                   env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})

    commands = (TREE / "src/battle_script_commands.c").read_text()
    patched = function(commands, "static void Cmd_jumpifplayerran(void)")
    if "CtrQol_ConfirmFlee" not in patched:
        raise SystemExit("run tools/bootstrap.py first to apply040 shiny faint-escape guard")
    # The comparison implementation comes from the pinned original game, not
    # a duplicate approximation of escape odds written for the test.
    original = subprocess.run(["git", "show", "HEAD:src/battle_script_commands.c"],
                              cwd=ROOT / "build/upstream", check=True, capture_output=True, text=True).stdout
    original = function(original, "static void Cmd_jumpifplayerran(void)")
    original = original.replace("Cmd_jumpifplayerran(void)", "Cmd_jumpifplayerran_original(void)")
    extracted = function((TREE / "src/battle_util.c").read_text(), "bool8 TryRunFromBattle(u8 battler)")
    extracted += function(commands, "static void Cmd_checkteamslost(void)")
    extracted += function(commands, "static void Cmd_openpartyscreen(void)") + patched + original
    (work / "flee_commands.inc").write_text(extracted)
    script = (TREE / "data/battle_scripts_1.s").read_text()
    faint = script.split("BattleScript_HandleFaintedMon::", 1)[1].split("BattleScript_FaintedMonTryChoose::", 1)[0]
    assert faint.index("checkteamslost ") < faint.index("jumpifbyte CMP_NOT_EQUAL, gBattleOutcome, 0") < faint.index("yesnobox") < faint.index("jumpifplayerran ")
    replacement = script.split("BattleScript_FaintedMonTryChoose::", 1)[1].lstrip().splitlines()[0]
    assert replacement == "openpartyscreen BS_FAINTED, BattleScript_FaintedMonEnd"
    subprocess.run([CC, *common, "-Wall", "-Wextra", "-Werror", "-pthread", *flags,
                    "-I" + str(ROOT / "android/host/test/stubs"), "-iquote" + str(work),
                    str(ROOT / "android/native/test/test_fainted_escape.c"),
                    str(ROOT / "android/native/src/qol_game.c"),
                    str(ROOT / "android/native/src/qol_rules.c"),
                    str(ROOT / "android/host/src/ctr_host.c"), str(TREE / "src/random.c"),
                    "-Wl,--gc-sections", "-o", str(work / "test_fainted_escape")], check=True)
    subprocess.run([str(work / "test_fainted_escape")], check=True, timeout=10,
                   env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
