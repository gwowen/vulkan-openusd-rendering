#!/usr/bin/env python3
"""Patch Slang's OpSource language and emit a C++ header of SPIR-V words."""

import pathlib
import struct
import sys


def rewrite_source_language(path: pathlib.Path) -> bytes:
    data = path.read_bytes()
    if len(data) < 20 or len(data) % 4 != 0:
        raise SystemExit(f"{path} is not a SPIR-V module")
    words = list(struct.unpack("<" + "I" * (len(data) // 4), data))
    if words[0] != 0x07230203:
        raise SystemExit(f"{path} is not little-endian SPIR-V")
    index = 5
    patched = False
    while index < len(words):
        word_count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        if word_count == 0 or index + word_count > len(words):
            raise SystemExit(f"{path} has a truncated SPIR-V instruction")
        if opcode == 3 and word_count >= 3:
            words[index + 1] = 0
            patched = True
            break
        index += word_count
    if not patched:
        raise SystemExit(f"{path} has no OpSource instruction to rewrite")
    rewritten = struct.pack("<" + "I" * len(words), *words)
    path.write_bytes(rewritten)
    return rewritten


def emit_array(name: str, blob: bytes) -> str:
    words = struct.unpack("<" + "I" * (len(blob) // 4), blob)
    lines = [f"inline constexpr std::uint32_t {name}[] = {{"]
    row = []
    for word in words:
        row.append(f"0x{word:08x}")
        if len(row) == 8:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: embed_spirv.py vertex.spv fragment.spv shader_spv.hpp")
    vertex = rewrite_source_language(pathlib.Path(sys.argv[1]))
    fragment = rewrite_source_language(pathlib.Path(sys.argv[2]))
    header = "\n".join(
        [
            "#pragma once",
            "#include <cstdint>",
            "",
            emit_array("kVertexSpirv", vertex),
            "",
            emit_array("kFragmentSpirv", fragment),
            "",
        ]
    )
    pathlib.Path(sys.argv[3]).write_text(header)


if __name__ == "__main__":
    main()
