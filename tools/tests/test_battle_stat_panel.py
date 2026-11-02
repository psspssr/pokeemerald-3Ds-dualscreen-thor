"""The upstream level-up classifier keeps BG1 in the UI composition."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class BattleStatPanelTests(unittest.TestCase):
    def test_actual_classifier_handles_priorities_and_disabled_layers(self):
        source = (ROOT / 'origin/3ds_port/src/3ds_video.c').read_text()
        start = source.index('static unsigned BattleBoxLayers(void)\n{')
        body = source[start:source.index('\n}\n', start)+3]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path/'test.c').write_text('#include <stdbool.h>\nstatic bool sBattle;\n'
                'static unsigned regs[16];\nstatic unsigned Reg(unsigned r){return regs[r];}\n'+body+
                'unsigned check(int battle,unsigned enabled,unsigned p0,unsigned p1){'
                'sBattle=battle;regs[0]=enabled?0x200:0;regs[8]=p0;regs[10]=p1;return BattleBoxLayers();}')
            subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test.so')],check=True)
            lib=ctypes.CDLL(str(path/'test.so'))
            for p0 in range(4):
                for p1 in range(4):
                    self.assertEqual(lib.check(1,1,p0,p1),2 if p1<p0 else 0)
                    self.assertEqual(lib.check(0,1,p0,p1),0)
                    self.assertEqual(lib.check(1,0,p0,p1),0)
        # The classifier must participate in both scene exclusion and UI draw.
        self.assertIn('sLayerExclude = 1u | leave | BattleBoxLayers()',source)
        self.assertIn('else if (sBattle && (bg == 0 || (BattleBoxLayers() & (1u << bg)))) DrawBattleTextLayer(bg);',source)
