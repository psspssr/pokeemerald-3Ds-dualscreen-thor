#!/usr/bin/env python3
"""Sanitize actual Save message expansion and glyph/layout bounds after overlays.

--before omits082 to reproduce the old overflow using the same source strings.
No save or game state is read or written.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from run_summary_tests import function
ROOT=Path(__file__).resolve().parents[3]
TREE=Path(os.environ.get("EMERALD_TEST_TREE",ROOT/"build/upstream")).resolve()
sys.path.insert(0,str(ROOT/"tools"))
from bootstrap import strict_apply


def encoded_strings():
    mapping={name.replace("\\'", "'"):bytes.fromhex(data) for name,data in
             re.findall(r"^'(.+)'\s*=\s*([0-9A-F ]+?)(?=\s*@|\s*$)",
                        (TREE/"charmap.txt").read_text(),re.M)}
    player=re.search(r"^PLAYER\s*=\s*([0-9A-F ]+)", (TREE/"charmap.txt").read_text(),re.M)[1]
    mapping["{PLAYER}"]=bytes.fromhex(player)
    text=(TREE/"data/text/save.inc").read_text()
    names=("DifferentSaveFile","ConfirmSave","AlreadySavedFile","PlayerSavedGame","SaveError")
    arrays=[]
    for name in names:
        block=text.split("gText_"+name+"::",1)[1].split("\ngText_",1)[0]
        string="".join(re.findall(r'\.string "(.*)"',block))
        encoded=bytearray()
        while string:
            if string.startswith("$"):
                encoded.append(255);string=string[1:]
            else:
                token=next((key for key in sorted(mapping,key=len,reverse=True) if string.startswith(key)),None)
                if token is None: raise ValueError("Unknown save-text encoding: "+repr(string[:20]))
                encoded.extend(mapping[token]);string=string[len(token):]
        arrays.append((name,encoded))
    return arrays,mapping


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--before",action="store_true")
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-save-ui-") as folder:
        work=Path(folder);rel="3ds_port/src/3ds_bottom_ui.c"
        path=work/rel;path.parent.mkdir(parents=True)
        path.write_bytes((ROOT/"origin"/rel).read_bytes())
        for patch in sorted((ROOT/"patches/android").glob("*.patch")):
            if args.before and patch.name.startswith("082-"): continue
            strict_apply(work,patch,["--include="+rel])
        source=path.read_text();strings,mapping=encoded_strings()
        capacity=re.search(r"u8 text\[(\d+)\];",source)[1]
        declarations=re.search(r"typedef struct\s*\{\s*const u16 \*glyphs;.*?\} Font;",source,re.S)[0]+"\n"
        declarations+=re.search(r"static u8 sSaveMessage\[\d+\];",source)[0]+"\n"
        declarations+="typedef struct { u8 saveStep,pressed,canSave,text["+capacity+"]; } ViewState;\n"
        fonts=(TREE/"src/fonts.c").read_text()
        for kind in ("Normal","Small"):
            declarations+=re.search(r"const u8 gFont"+kind+r"LatinGlyphWidths\[\] = \{.*?\};",fonts,re.S)[0]+"\n"
        (work/"save_data.inc").write_text(declarations+"\n".join(
            "static const u8 text"+name+"[]={"+",".join(map(str,data))+"};" for name,data in strings)+
            "\nstatic const u8 playerName[]={"+",".join(map(str,mapping['W']*7+b'\xff'))+"};\n")
        util=(TREE/"src/string_util.c").read_text()
        helpers=function(util,"u8 *StringExpandPlaceholders(u8 *dest, const u8 *src)")
        helpers+=function(util,"u8 GetExtCtrlCodeLength(u8 code)")
        for signature in ("static void ResolveFonts(void)","static u16 NextGlyph(const u8 **str)",
                          "static int DrawStr(const Font *font, const u8 *str, int x, int y, u16 fg, u16 shadow)",
                          "static void CopyText(u8 *dst, int size, const u8 *src)"):
            helpers+=function(source,signature)
        if "static bool8 AppendSaveMessage(" in source:
            helpers+=function(source,"static bool8 AppendSaveMessage(u8 **out, unsigned *remaining, const u8 *text, unsigned depth)")
            helpers+=function(source,"static void SetSaveMessage(const u8 *text)")
            assert "StringExpandPlaceholders(sSaveMessage" not in source
            assert source.count("SetSaveMessage(")==4 # definition plus all3 Save writes
        else:
            helpers+="static void SetSaveMessage(const u8 *text) { StringExpandPlaceholders(sSaveMessage,text); }\n"
        helpers+=function(source,"static void DrawSave(const ViewState *s)")
        helpers+=function(source,"static void DoSave(void)")
        helpers+=function(source,"static void OpenSave(void)")
        activate=function(source,"static void Activate(u8 id, u8 mode)")
        save_case=activate[activate.index("    case SCR_SAVE:"):activate.index("    case SCR_OPTION:")]
        helpers+="static void ActivateSave(u8 id) { switch (sScreen) {\n"+save_case+"default: break;} }\n"
        (work/"save_helpers.inc").write_text(helpers)
        binary=work/"save-ui"
        subprocess.run([os.environ.get("CC","cc"),"-std=gnu11","-O1","-g","-Wall","-Wextra","-Werror",
                        "-Wno-implicit-fallthrough","-fsanitize=address,undefined","-fno-omit-frame-pointer",
                        "-DTEST_BOUNDED="+str(int("static bool8 AppendSaveMessage(" in source)),
                        "-I"+str(work),"-iquote"+str(TREE/"include"),
                        str(ROOT/"android/native/test/test_save_ui.c"),"-o",str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=10,env={**os.environ,"UBSAN_OPTIONS":"halt_on_error=1"})


if __name__=="__main__": main()
