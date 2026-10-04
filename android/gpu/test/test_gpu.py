#!/usr/bin/env python3
"""Host regressions for PICA textures, shader translation and frame deadlines."""
import ctypes
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
class FrameSchedule(ctypes.Structure):
    _fields_ = [("speed", ctypes.c_uint), ("remaining", ctypes.c_uint)]

spec = importlib.util.spec_from_file_location("picasso", ROOT / "tools/picasso2glsl.py")
picasso = importlib.util.module_from_spec(spec)
spec.loader.exec_module(picasso)


class CpuBackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "decode.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "android/gpu/include"),
                        str(ROOT / "android/gpu/src/texture_decode.c"),
                        str(ROOT / "android/gpu/src/pacing.c"), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.gpuDecodeTexture.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_int]
        cls.lib.gpuDecodeTexture.restype = ctypes.c_bool
        cls.lib.gpuPacingDeadline.argtypes = [ctypes.c_double, ctypes.c_double, ctypes.c_double]
        cls.lib.gpuPacingDeadline.restype = ctypes.c_double
        cls.lib.gpuFrameDue.argtypes = [ctypes.POINTER(FrameSchedule), ctypes.c_uint]
        cls.lib.gpuFrameDue.restype = ctypes.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def decode(self, data, width, height, fmt):
        src = ctypes.create_string_buffer(data)
        dst = ctypes.create_string_buffer(width * height * 4)
        self.assertTrue(self.lib.gpuDecodeTexture(src, dst, width, height, fmt))
        return dst.raw

    def test_rgba5551_bit_width_and_alpha(self):
        for packed, expected in [(0xF801, (255, 0, 0, 255)), (0x07C1, (0, 255, 0, 255)),
                                 (0x003F, (0, 0, 255, 255)), (0xFFFF, (255, 255, 255, 255)),
                                 (0xFFFE, (255, 255, 255, 0)), (0x0843, (8, 8, 8, 255))]:
            with self.subTest(packed=packed):
                self.assertEqual(self.decode(struct.pack("<H", packed) * 64, 8, 8, 2)[:4], bytes(expected))

    def test_rgb565_green_has_six_bits(self):
        self.assertEqual(self.decode(struct.pack("<H", 0x07E0) * 64, 8, 8, 3)[:4], bytes((0, 255, 0, 255)))

    def test_rgba8_is_abgr_memory(self):
        self.assertEqual(self.decode(bytes((0x44, 0x33, 0x22, 0x11)) * 64, 8, 8, 0)[:4], bytes((0x11, 0x22, 0x33, 0x44)))

    def test_morton_tiles_and_top_to_bottom_orientation(self):
        # Independent explicit 8x8 Morton index table (PICA interleaves x then y).
        order = [0,1,4,5,16,17,20,21,2,3,6,7,18,19,22,23,
                 8,9,12,13,24,25,28,29,10,11,14,15,26,27,30,31,
                 32,33,36,37,48,49,52,53,34,35,38,39,50,51,54,55,
                 40,41,44,45,56,57,60,61,42,43,46,47,58,59,62,63]
        source = bytearray(16 * 16)
        for y in range(16):
            for x in range(16):
                tile = (y // 8) * 2 + x // 8
                source[tile * 64 + order[(y % 8) * 8 + x % 8]] = y * 16 + x
        rgba = self.decode(bytes(source), 16, 16, 7)
        for y in range(16):
            for x in range(16):
                pixel = rgba[((15-y)*16+x)*4:((15-y)*16+x)*4+4]
                self.assertEqual(pixel, bytes((y*16+x,)*3 + (255,)))

    def test_nibble_luminance_and_alpha(self):
        lum = self.decode(b"\xf0" * 32, 8, 8, 10)
        alpha = self.decode(b"\xf0" * 32, 8, 8, 11)
        self.assertEqual(lum[:8], bytes((0,0,0,255,255,255,255,255)))
        self.assertEqual(alpha[:8], bytes((255,255,255,0,255,255,255,255)))

    def test_unsupported_compression_rejected(self):
        data = ctypes.create_string_buffer(256)
        self.assertFalse(self.lib.gpuDecodeTexture(data, data, 8, 8, 12))
        self.assertFalse(self.lib.gpuDecodeTexture(data, data, 7, 8, 0))

    def test_under_budget_frames_keep_59_83_hz_cadence(self):
        period = 1.0 / 59.83
        start = previous = 1000.0
        for frame in range(1, 601):
            now = previous + 0.004 + (frame % 7) * 0.0005
            deadline = self.lib.gpuPacingDeadline(now, previous, period)
            self.assertGreater(deadline, now)
            self.assertAlmostEqual(deadline, start + frame * period, places=9)
            previous = deadline

    def test_over_budget_frames_never_add_a_sleep(self):
        period = 1.0 / 59.83
        for work_periods in (1.01, 2, 4, 5, 20):
            with self.subTest(work_periods=work_periods):
                previous = 1000.0
                now = previous + work_periods * period
                self.assertLessEqual(self.lib.gpuPacingDeadline(now, previous, period), now)

    def test_sustained_slow_presentation_has_no_periodic_extra_wait(self):
        period = 1.0 / 59.83
        now = previous = 1000.0
        for _ in range(600):
            now += 0.04
            previous = self.lib.gpuPacingDeadline(now, previous, period)
            self.assertEqual(max(0, previous - now), 0)
        self.assertAlmostEqual(now, 1024.0, places=8)

    def test_resume_starts_new_phase_with_remaining_frame_budget(self):
        period = 1.0 / 59.83
        resume = 10000.0
        # The lifecycle callback anchors previous to resume. Rendering for6ms
        # consumes that budget; pacing waits the remainder, not a fresh period.
        deadline = self.lib.gpuPacingDeadline(resume + 0.006, resume, period)
        self.assertAlmostEqual(deadline, resume + period)
        self.assertAlmostEqual(deadline - resume - 0.006, period - 0.006)
        # Work that already exceeds the resumed frame budget doesn't sleep.
        self.assertLessEqual(self.lib.gpuPacingDeadline(resume + 0.080, resume, period), resume + 0.080)

    def test_speed_groups_simulation_ticks_without_extra_presentations(self):
        for speed in (1, 2, 3, 4, 8):
            with self.subTest(speed=speed):
                schedule = FrameSchedule()
                shown = [tick for tick in range(600 * speed)
                         if self.lib.gpuFrameDue(ctypes.byref(schedule), speed)]
                self.assertEqual(shown, list(range(0, 600 * speed, speed)))
                # Every group consumes one unchanged display-frame deadline.
                previous = 1000.0
                for _ in shown:
                    previous = self.lib.gpuPacingDeadline(previous + 0.006, previous, 1 / 59.83)
                self.assertAlmostEqual(previous, 1000 + 600 / 59.83, places=9)

    def test_speed_changes_and_resume_show_an_immediate_frame(self):
        schedule = FrameSchedule()
        for speed in (4, 2, 8, 3, 1, 8, 4):
            self.assertTrue(self.lib.gpuFrameDue(ctypes.byref(schedule), speed))
            self.assertEqual(schedule.remaining, speed - 1)
        # Lifecycle clears the schedule; no pre-pause skip survives resumption.
        schedule = FrameSchedule()
        self.assertTrue(self.lib.gpuFrameDue(ctypes.byref(schedule), 4))
        self.assertFalse(self.lib.gpuFrameDue(ctypes.byref(schedule), 4))

    def test_invalid_speed_falls_back_to_normal_cadence(self):
        schedule = FrameSchedule()
        for speed in (0, 9, 100):
            for _ in range(5):
                self.assertTrue(self.lib.gpuFrameDue(ctypes.byref(schedule), speed))
                self.assertEqual(schedule.speed, 1)


class ShaderTests(unittest.TestCase):
    def test_upstream_voxel_shader_and_uniform_contract(self):
        source = (ROOT / "origin/3ds_port/src/voxel/voxel.v.pica").read_text()
        glsl, uniforms = picasso.translate(source)
        self.assertEqual(uniforms, [("projection",0),("modelView",4),("shadeTint",8),("tintDiff",9),
                                    ("grade",10),("fog",11),("dappleU",12),("dappleV",13)])
        self.assertIn("dot(u[4],r0)", glsl)
        self.assertIn("-2.0*p_position.z-p_position.w", glsl)
        binary = picasso.compile_shader(source)
        magic, length, count = struct.unpack_from("<8sII", binary)
        self.assertEqual(magic, b"CTRGLS1\0")
        self.assertEqual(len(binary), 16 + count*68 + length)
        self.assertEqual(binary[16+count*68:], glsl.encode()+b"\0")

    def test_unknown_instruction_is_build_error(self):
        source = ".out p position\n.proc main\ncall missing\n.end\n"
        with self.assertRaisesRegex(ValueError, "line 3"):
            picasso.compile_shader(source)

    def test_uniform_overflow_is_build_error(self):
        with self.assertRaisesRegex(ValueError, "register allocation"):
            picasso.compile_shader(".fvec projection[97]\n")

    def test_uniform_name_cannot_be_silently_truncated(self):
        with self.assertRaisesRegex(ValueError, "63-byte limit"):
            picasso.compile_shader(".fvec " + "long_name"*8 + "\n")

    def test_partial_write_retains_destination_mask(self):
        source = ".out p position\n.alias a v0\n.proc main\nmov p.xyz, a\nmov p.w, a.wwww\nend\n.end\n"
        glsl, _ = picasso.translate(source)
        self.assertIn("p_position.xyz = ((v0)).xyz;", glsl)
        self.assertIn("p_position.w = (((v0).wwww)).w;", glsl)


class VoxelUploadQueueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="emerald-upload-queue-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        source = (ROOT / "origin/3ds_port/src/3ds_video.c").read_text()
        begin = source.index("static gxCmdQueue_s *sFrameQueue;")
        end = source.index("\n/* The voxel overworld", begin)
        fixture = work / "queue.c"
        fixture.write_text('#include "gx_queue.h"\n#include <stddef.h>\n' + source[begin:end] + r'''
static gxCmdQueue_s queue;
void reset(unsigned reserve)
{
    queue=(gxCmdQueue_s){.maxEntries=32}; sUploadCommands=0; sRenderReserve=reserve;
    GX_BindQueue(&queue);
}
unsigned used(void) { return queue.numEntries; }
void unbind(void) { GX_BindQueue(NULL); }
''')
        library = work / "queue.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "android/gpu/src"),
                        "-I" + str(ROOT / "android/gpu/include"),
                        "-I" + str(ROOT / "android/shim/include"),
                        str(fixture), str(ROOT / "android/gpu/src/gx_queue.c"),
                        "-Wl,--wrap=GX_BindQueue", "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.reset.argtypes = [ctypes.c_uint]
        cls.lib.CtrVideo_TryVoxelUpload.restype = ctypes.c_bool
        cls.lib.CtrVideo_VoxelUploadsLeft.restype = ctypes.c_uint
        cls.lib.used.restype = ctypes.c_uint

    def test_world_and_battle_allowances_use_upstream_reserve(self):
        for reserve, allowance in ((24, 4), (28, 2)):
            self.lib.reset(reserve)
            self.assertEqual(self.lib.CtrVideo_VoxelUploadsLeft(), allowance)
            for left in range(allowance - 1, -1, -1):
                self.assertTrue(self.lib.CtrVideo_TryVoxelUpload())
                self.lib.gpuGxRecordCommand()  # split
                self.lib.gpuGxRecordCommand()  # texture copy
                self.assertEqual(self.lib.CtrVideo_VoxelUploadsLeft(), left)
            self.assertFalse(self.lib.CtrVideo_TryVoxelUpload())

    def test_other_commands_reduce_capacity_and_counts_saturate(self):
        self.lib.reset(24)
        for _ in range(4):
            self.lib.gpuGxRecordCommand()
        self.assertEqual(self.lib.CtrVideo_VoxelUploadsLeft(), 2)
        for _ in range(40):
            self.lib.gpuGxRecordCommand()
        self.assertEqual(self.lib.used(), 32)
        self.assertFalse(self.lib.CtrVideo_TryVoxelUpload())
        self.assertEqual(self.lib.CtrVideo_VoxelUploadsLeft(), 0)

    def test_unbound_queue_never_claims_upload_capacity(self):
        self.lib.reset(24)
        self.lib.unbind()
        self.lib.gpuGxRecordCommand()
        self.assertEqual(self.lib.used(), 0)
        self.assertFalse(self.lib.CtrVideo_TryVoxelUpload())
        self.assertEqual(self.lib.CtrVideo_VoxelUploadsLeft(), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
