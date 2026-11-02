#!/usr/bin/env python3
"""Exercise the current bottom screen's dock, native views and touch routing."""
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[3]
TREE=Path(os.environ.get('EMERALD_TEST_TREE',ROOT/'build/upstream'))

def main():
    for name in ('bottom_map_test.py','bottom_taps_test.py'):
        subprocess.run([sys.executable,str(TREE/'3ds_port/tests'/name),'--cc',os.environ.get('CC','cc')],check=True)
    source=(TREE/'3ds_port/src/3ds_bottom_ui.c').read_text()
    start=source.index('static u8 CurrentMode(void)\n{')
    end=source.index('\n}',start)
    mode=source[start:end]
    assert mode.index('return MODE_PARTY_MENU;')<mode.index('CtrStorage_IsOpen() || CtrVideo_BottomWhole()')
    # Full native UI is presented without the obsolete cropped GBA mapping.
    assert 'CtrBottomContent_SourcePoint' not in source
    print('PASS upstream native bottom screen and Android whole-party routing')
if __name__=='__main__':main()
