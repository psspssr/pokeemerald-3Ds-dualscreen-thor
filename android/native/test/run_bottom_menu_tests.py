#!/usr/bin/env python3
"""Exercise the current bottom screen's dock, native views and touch routing."""
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[3]
TREE=Path(os.environ.get('EMERALD_TEST_TREE',ROOT/'build/upstream'))
PORT=TREE/'3ds_port'
if not (PORT/'tests/bottom_map_test.py').is_file():
    subprocess.run([sys.executable,str(ROOT/'tools/bootstrap.py'),'--dir',str(TREE)],check=True)

def main():
    for name in ('bottom_map_test.py','bottom_taps_test.py'):
        subprocess.run([sys.executable,str(PORT/'tests'/name),'--cc',os.environ.get('CC','cc')],check=True)
    source=(PORT/'src/3ds_bottom_ui.c').read_text()
    start=source.index('static u8 CurrentMode(void)\n{')
    end=source.index('\n}',start)
    mode=source[start:end]
    assert mode.index('return MODE_PARTY_MENU;')<mode.index('CtrStorage_IsOpen() || CtrVideo_BottomWhole()')
    # Full native UI is presented without the obsolete cropped GBA mapping.
    assert 'CtrBottomContent_SourcePoint' not in source
    video=(PORT/'src/3ds_video.c').read_text()
    assert 'CtrGpu_ConfigureVoxelTargets(sLogical, sTop, !down && (voxel || sBattleWorld), bottom);' in video
    print('PASS upstream native bottom screen, held-top/Pokédex scaling scope and Android whole-party routing')
if __name__=='__main__':main()
