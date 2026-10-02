#!/usr/bin/env python3
"""Host regressions for byte-exact PICA texture layout and shader translation."""
import ctypes
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("picasso", ROOT / "tools/picasso2glsl.py")
picasso = importlib.util.module_from_spec(spec)
spec.loader.exec_module(picasso)


class TextureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "decode.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "android/gpu/include"),
                        str(ROOT / "android/gpu/src/texture_decode.c"), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.gpuDecodeTexture.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_int]
        cls.lib.gpuDecodeTexture.restype = ctypes.c_bool

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

    def test_partial_write_retains_destination_mask(self):
        source = ".out p position\n.alias a v0\n.proc main\nmov p.xyz, a\nmov p.w, a.wwww\nend\n.end\n"
        glsl, _ = picasso.translate(source)
        self.assertIn("p_position.xyz = ((v0)).xyz;", glsl)
        self.assertIn("p_position.w = (((v0).wwww)).w;", glsl)


if __name__ == "__main__":
    unittest.main(verbosity=2)
