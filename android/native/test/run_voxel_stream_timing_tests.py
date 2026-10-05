#!/usr/bin/env python3
"""Run the real Android frame-end and upstream streaming budget on a fake clock.

--before omits101 and demonstrates that presentation pacing consumes the entire
post-submit budget. Only temporary copies of the two upstream files are patched.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from run_summary_tests import function

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from bootstrap import patch_files, strict_apply


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', action='store_true')
    args = parser.parse_args()
    paths = ('3ds_port/src/3ds_video.c', '3ds_port/src/voxel/ctr_voxel.c')
    with tempfile.TemporaryDirectory(prefix='emerald-stream-timing-') as folder:
        work = Path(folder)
        for relative in paths:
            dest = work / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes((ROOT / 'origin' / relative).read_bytes())
        for patch in sorted((ROOT / 'patches/android').glob('*.patch')):
            if args.before and patch.name.startswith('101-'):
                continue
            if set(paths) & set(patch_files(patch)):
                strict_apply(work, patch, ['--include=' + name for name in paths])
        video = (work / paths[0]).read_text()
        voxel = (work / paths[1]).read_text()
        core = (ROOT / 'android/gpu/src/core.c').read_text()
        display = (ROOT / 'android/gpu/src/display.c').read_text()
        # The production early return precedes this tail. The fixture drives
        # the actual FrameBegin to check its real1/2/4/8 skipped-frame schedule.
        present = function(video, 'void CtrVideo_Present(void)')
        start = present.index('    PORT_PROF_BEGIN(frameEnd);')
        stop = present.index('    uint64_t now = CtrPlatform_Milliseconds();', start)
        assert present.index('if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) return;') < start
        (work / 'video_tail.inc').write_text(present[start:stop])
        hook = 'static void VoxelAfterSubmitBeforePacing(void *parameter)'
        (work / 'video_hook.inc').write_text(function(video, hook) if hook in video else '')
        shutdown = function(video, 'void CtrVideo_Shutdown(void)')
        shutdown_prefix = shutdown[shutdown.index('{') + 1:shutdown.index('    BottomRelease();')]
        (work / 'shutdown_hook.inc').write_text(shutdown_prefix)
        (work / 'core.inc').write_text('\n'.join(function(core, name) for name in (
            'bool C3D_FrameBegin(u8 flags)', 'void C3D_FrameEnd(u8 flags)',
        )))
        budget = 'float CtrGpu_FrameTimeLeftMs(void)'
        helper = function(display, budget) if budget in display else 'float CtrGpu_FrameTimeLeftMs(void) { return 0; }\n'
        (work / 'display.inc').write_text('\n'.join(function(display, name) for name in (
            'static void lifecycle(CtrAptEvent event,void *user)',
            'bool gpuShouldRender(void)', 'void gpuPace(void)',
        )) + helper)
        constants = ('VOXEL_FRAME_MS', 'VOXEL_AFTER_MAX_MS', 'VOXEL_AFTER_MARGIN_MS',
                     'VOXEL_OTHERS_CAP_MS', 'VOXEL_CROSSING_RESERVE_MS')
        definitions = '\n'.join(re.search(r'^#define ' + name + r'\s+.*$', voxel, re.M)[0] for name in constants)
        (work / 'voxel.inc').write_text(definitions + '\n' + '\n'.join(function(voxel, name) for name in (
            'static float MsSince(uint64_t start)', 'static float TicksMs(uint64_t ticks)',
            'static float Clamp(float value, float lo, float hi)',
            'void CtrVoxel_AfterSubmit(uint64_t frameBeginTick)',
        )))
        binary = work / 'voxel-stream-timing'
        subprocess.run([
            os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L',
            '-D__ANDROID__', '-DCTR_VOXEL_ENABLED=1', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I' + str(work), '-I' + str(ROOT / 'android/gpu/src'),
            '-I' + str(ROOT / 'android/host/include'),
            str(ROOT / 'android/native/test/test_voxel_stream_timing.c'),
            str(ROOT / 'android/gpu/src/pacing.c'), '-lm', '-o', str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True, timeout=10,
                       env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})


if __name__ == '__main__':
    main()
