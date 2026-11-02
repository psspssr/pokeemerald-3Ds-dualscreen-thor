"""Evolution shares the upstream battle coordinate system in both units."""
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class EvolutionScreenTests(unittest.TestCase):
    def test_real_make_flags_and_callback_redirection(self):
        source=(ROOT/'origin/3ds_port/full.mk').read_text()
        rules=source[source.index('CTR_GBA_BATTLE_SRCS :='):source.index('# The battle transitions:')]
        with tempfile.TemporaryDirectory() as folder:
            tree=Path(folder)
            (tree/'compat').mkdir()
            (tree/'include/gba').mkdir(parents=True)
            (tree/'include/gba/defines.h').touch()
            (tree/'compat/ctr_gba_battle.h').write_bytes((ROOT/'origin/3ds_port/compat/ctr_gba_battle.h').read_bytes())
            targets=['build/root/src/evolution_scene.o','build/root/src/evolution_graphics.o']
            (tree/'Makefile').write_text(rules+'\n'+' '.join(targets)+":\n\t@printf '%s\\n' '$(FULLCFLAGS)'\n")
            flags=[]
            for target in targets:
                out=subprocess.check_output(['make','-s','--no-print-directory','ROOT='+str(tree),target],cwd=tree,text=True)
                flags.append(shlex.split(out))
            self.assertEqual(flags[0],flags[1]);self.assertIn('-DCTR_GBA_STAGE',flags[0])
            (tree/'test.c').write_text('void scene(void){ SetVBlankCallback(normal); }\n')
            out=subprocess.check_output(['cc','-E','-P',*flags[0],str(tree/'test.c')],text=True)
            self.assertIn('CtrBattle_SetVBlankCallback(normal)',out)
