#!/usr/bin/env python3
"""Translate the straight-line PICA vertex programs used by origin to GLES 3.

This is a compiler, not a hardcoded replacement for voxel.v.pica. Unsupported
instructions fail the build with a source line, so upstream shader changes can
never silently become an incorrect Android shader. The container is read by
android/gpu/src/shader.c; all lengths and register indices are checked there.
"""
from __future__ import annotations
import argparse
import re
import struct
from pathlib import Path

MAGIC = b"CTRGLS1\0"


def translate(source: str) -> tuple[str, list[tuple[str, int]]]:
    symbols: dict[str, str] = {}
    uniforms: list[tuple[str, int]] = []
    constants: list[str] = []
    instructions: list[str] = []
    register = 0
    inside = False
    outputs = {"position": "p_position", "color": "v_color", "texcoord0": "v_tex0",
               "texcoord1": "v_tex1", "texcoord2": "v_tex2"}

    def expr(token: str, depth: int = 0) -> str:
        if depth > 16:
            raise ValueError("cyclic alias")
        token = token.strip()
        negative = token.startswith("-")
        if negative:
            token = token[1:]
        match = re.fullmatch(r"([A-Za-z_]\w*(?:\[\d+\])?)(?:\.([xyzw]{1,4}))?", token)
        if not match:
            raise ValueError(f"unsupported operand: {token}")
        name, swizzle = match.groups()
        array = re.fullmatch(r"(\w+)\[(\d+)\]", name)
        if array and array[1] in symbols and symbols[array[1]].startswith("u["):
            base = int(symbols[array[1]][2:-1]) + int(array[2])
            if not 0 <= base < 96:
                raise ValueError("uniform register overflow")
            value = f"u[{base}]"
        elif name in symbols:
            value = symbols[name]
            if not (value.startswith("u[") or value.startswith("p_") or value.startswith("v_") or value.startswith("k_")):
                value = expr(value, depth + 1)
        elif re.fullmatch(r"(?:r|v)\d+", name):
            index = int(name[1:])
            if index >= 16:
                raise ValueError("vertex or temporary register overflow")
            value = name
        else:
            raise ValueError(f"unknown operand: {name}")
        if swizzle:
            swizzle = (swizzle + swizzle[-1] * 4)[:4]
            value = f"({value}).{swizzle}"
        return f"(-({value}))" if negative else value

    for number, raw in enumerate(source.splitlines(), 1):
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        try:
            if line.startswith(".fvec "):
                for decl in line[6:].split(","):
                    match = re.fullmatch(r"\s*(\w+)(?:\[(\d+)\])?\s*", decl)
                    if not match:
                        raise ValueError("invalid uniform declaration")
                    name, count = match[1], int(match[2] or 1)
                    if not count or register + count > 96 or name in symbols:
                        raise ValueError("invalid uniform register allocation")
                    symbols[name] = f"u[{register}]"
                    uniforms.append((name, register))
                    register += count
            elif line.startswith(".constf "):
                match = re.fullmatch(r"\.constf\s+(\w+)\(([^)]+)\)", line)
                if not match:
                    raise ValueError("invalid constant")
                values = [str(float(v.strip())) for v in match[2].split(",")]
                if len(values) != 4:
                    raise ValueError("constant must have four components")
                symbols[match[1]] = "k_" + match[1]
                constants.append(f"const vec4 k_{match[1]} = vec4({','.join(values)});")
            elif line.startswith(".alias "):
                _, name, value = line.split()
                symbols[name] = value
            elif line.startswith(".out "):
                _, name, semantic = line.split()
                if semantic not in outputs:
                    raise ValueError(f"unsupported output semantic: {semantic}")
                symbols[name] = outputs[semantic]
            elif line == ".proc main":
                inside = True
            elif line == ".end":
                inside = False
            elif line in ("end", "nop"):
                continue
            elif line.startswith(".") or not inside:
                raise ValueError(f"unsupported declaration: {line}")
            else:
                op, arguments = line.split(None, 1)
                args = [a.strip() for a in arguments.split(",")]
                dest = args[0].split(".")
                dst = symbols.get(dest[0], dest[0])
                if not (re.fullmatch(r"r(?:[0-9]|1[0-5])", dst) or dst in outputs.values()):
                    raise ValueError(f"invalid destination: {args[0]}")
                mask = dest[1] if len(dest) == 2 else "xyzw"
                if not re.fullmatch(r"[xyzw]{1,4}", mask):
                    raise ValueError("invalid destination mask")
                values = [expr(a) for a in args[1:]]
                unary = {"mov": "({0})", "rcp": "(vec4(1.0)/({0}))", "rsq": "inversesqrt(abs({0}))",
                         "flr": "floor({0})", "abs": "abs({0})", "ex2": "exp2({0})", "lg2": "log2({0})"}
                binary = {"add": "(({0})+({1}))", "mul": "(({0})*({1}))", "min": "min({0},{1})",
                          "max": "max({0},{1})", "dp3": "vec4(dot(({0}).xyz,({1}).xyz))",
                          "dp4": "vec4(dot({0},{1}))", "slt": "vec4(lessThan({0},{1}))",
                          "sge": "vec4(greaterThanEqual({0},{1}))"}
                if op in unary and len(values) == 1:
                    rhs = unary[op].format(*values)
                elif op in binary and len(values) == 2:
                    rhs = binary[op].format(*values)
                elif op == "mad" and len(values) == 3:
                    rhs = "(({0})*({1})+({2}))".format(*values)
                else:
                    raise ValueError(f"unsupported instruction or arity: {op}")
                instructions.append(f"    {dst}.{mask} = ({rhs}).{mask};")
        except (ValueError, KeyError) as error:
            raise ValueError(f"line {number}: {error}: {raw.strip()}") from error
    if not instructions or "p_position" not in symbols.values():
        raise ValueError("missing main program or position output")
    glsl = "#version 300 es\nprecision highp float;\nuniform vec4 u[96];\n"
    glsl += "\n".join(f"layout(location={i}) in vec4 v{i};" for i in range(16)) + "\n"
    glsl += "out vec4 v_color;\nout vec4 v_tex0;\nout vec4 v_tex1;\nout vec4 v_tex2;\n"
    glsl += "\n".join(constants) + "\nvoid main() {\nvec4 p_position=vec4(0.0);\n"
    glsl += "v_color=vec4(1.0); v_tex0=vec4(0.0); v_tex1=vec4(0.0); v_tex2=vec4(0.0);\n"
    glsl += "\n".join(f"vec4 r{i}=vec4(0.0);" for i in range(16)) + "\n"
    glsl += "\n".join(instructions)
    # PICA clip Z is [-w,0]; Citro3D's default depth map puts near at 1.
    glsl += "\ngl_Position=vec4(p_position.xy,-2.0*p_position.z-p_position.w,p_position.w);\n}\n"
    return glsl, uniforms


def compile_shader(source: str) -> bytes:
    glsl, uniforms = translate(source)
    body = glsl.encode() + b"\0"
    table = b"".join(struct.pack("<64sI", name.encode(), index) for name, index in uniforms)
    return struct.pack("<8sII", MAGIC, len(body), len(uniforms)) + table + body


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        data = compile_shader(args.source.read_text())
    except ValueError as error:
        parser.exit(1, f"{args.source}: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)


if __name__ == "__main__":
    main()
