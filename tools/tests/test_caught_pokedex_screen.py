"""Exercise upstream caught-page detection throughout task allocation/fade-out."""
import ctypes
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class CaughtPokedexScreenTests(unittest.TestCase):
    def test_registration_page_lifecycle(self):
        patch=(ROOT/'origin/patches/pokeemerald/0054-pokedex-capture-layout.patch').read_text()
        added='\n'.join(line[1:] for line in patch.splitlines() if line.startswith('+') and not line.startswith('+++'))
        start=added.index('bool8 CtrPokedex_IsCaughtMonPage(void)')
        body=added[start:added.index('\n}',start)+2]
        with tempfile.TemporaryDirectory() as folder:
            tree=Path(folder)
            (tree/'test.c').write_text('''#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8, bool8;
#define TASK_NONE 255
static int Task_DisplayCaughtMonDexPage, Task_HandleCaughtMonPageInput=1, Task_ExitCaughtMonPage=2;
static struct {int data[16];} gTasks[16];
static u8 taskId=TASK_NONE; static bool input,exitTask;
static u8 FindTaskIdByFunc(int f){(void)f;return taskId;}
static bool FuncIsActiveTask(int f){return f==Task_HandleCaughtMonPageInput?input:exitTask;}
'''+body+'''
int check(int id,int state,int i,int e){taskId=id;gTasks[0].data[0]=state;input=i;exitTask=e;return CtrPokedex_IsCaughtMonPage();}
''')
            subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',str(tree/'test.c'),'-o',str(tree/'test.so')],check=True)
            lib=ctypes.CDLL(str(tree/'test.so'))
            self.assertEqual(lib.check(255,0,0,0),0)
            self.assertEqual(lib.check(255,0,1,0),1)
            self.assertEqual(lib.check(255,0,0,1),1)
            self.assertEqual(lib.check(0,1,0,0),1)
