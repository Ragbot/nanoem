#!/usr/bin/env python3
"""
Generates emapp/test/fixtures/test_vertex_morph.pmx, a minimal PMX 2.0 fixture with
3 vertices, 1 face, 1 material, 1 bone and 1 vertex morph (offsets vertex 0 by +1 on X).

The byte layout mirrors nanoem's PMX reader (nanoem/nanoem.c) exactly:
  * header: info_length=8, 8 global bytes (index sizes are literal byte counts),
    4 texts (name_ja, name_en, comment_ja, comment_en)
  * vertex: origin, normal, uv, [no additional uv], deform type (1 byte), deform
    payload (BDEF1 -> 4 byte bone index), edge size
  * version <= 2.0 must end exactly after the joint count (no softbody block)

Run from anywhere: python3 scripts/generate_test_vertex_morph_pmx.py
"""
import os
import struct


def text(s):
    b = s.encode("utf-16-le")
    return struct.pack("<i", len(b)) + b


def main():
    out = bytearray()
    out += b"PMX "
    out += struct.pack("<f", 2.0)
    out += struct.pack("<B", 8)  # info_length
    # codec=UTF16LE, addUV=0, vertex idx=2B, texture idx=1B, material idx=1B,
    # bone idx=4B, morph idx=1B, rigid body idx=1B
    out += bytes([0, 0, 2, 1, 1, 4, 1, 1])
    out += text("テスト")
    out += text("")
    out += text("")
    out += text("")

    # vertices (BDEF1: type byte + 4 byte bone index, edge size last)
    out += struct.pack("<i", 3)
    for pos in ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)):
        out += struct.pack("<3f", *pos)
        out += struct.pack("<3f", 0.0, 0.0, 1.0)
        out += struct.pack("<2f", 0.0, 0.0)
        out += struct.pack("<B", 0)  # BDEF1
        out += struct.pack("<i", 0)  # bone index 0 (4 bytes)
        out += struct.pack("<f", 1.0)

    # faces: 3 indices (2 bytes each)
    out += struct.pack("<i", 3)
    out += struct.pack("<3H", 0, 1, 2)

    # textures: none
    out += struct.pack("<i", 0)

    # materials: 1
    out += struct.pack("<i", 1)
    out += text("マテリアル")
    out += text("")
    out += struct.pack("<4f", 1.0, 1.0, 1.0, 1.0)  # diffuse rgba
    out += struct.pack("<3f", 0.0, 0.0, 0.0)  # specular rgb
    out += struct.pack("<f", 0.0)  # specular power
    out += struct.pack("<3f", 0.5, 0.5, 0.5)  # ambient rgb
    out += struct.pack("<B", 0)  # flags
    out += struct.pack("<3f", 1.0, 1.0, 1.0)  # edge color rgb
    out += struct.pack("<f", 1.0)  # edge opacity
    out += struct.pack("<f", 1.0)  # edge size
    out += struct.pack("<B", 0)  # diffuse texture index (1 byte)
    out += struct.pack("<B", 0)  # sphere texture index (1 byte)
    out += struct.pack("<B", 0)  # sphere type = NONE
    out += struct.pack("<B", 1)  # is_toon_shared
    out += struct.pack("<B", 0)  # shared toon texture index (1 byte)
    out += text("")
    out += struct.pack("<i", 3)  # num vertex indices

    # bones: 1 (flags=0 -> destination origin follows)
    out += struct.pack("<i", 1)
    out += text("ボーン")
    out += text("")
    out += struct.pack("<3f", 0.0, 0.0, 0.0)
    out += struct.pack("<i", -1)
    out += struct.pack("<i", 0)
    out += struct.pack("<H", 0)
    out += struct.pack("<3f", 0.0, 0.0, 0.0)

    # morphs: 1 vertex morph offsetting vertex 0 by +1 on X
    out += struct.pack("<i", 1)
    out += text("頂点モーフ")
    out += text("vertex morph")
    out += struct.pack("<B", 0)  # category = BASE
    out += struct.pack("<B", 1)  # type = VERTEX
    out += struct.pack("<i", 1)  # 1 child
    out += struct.pack("<H", 0)  # vertex index 0 (2 bytes)
    out += struct.pack("<3f", 1.0, 0.0, 0.0)

    # labels, rigid bodies, joints: none (version 2.0 -> no softbody block)
    out += struct.pack("<i", 0)
    out += struct.pack("<i", 0)
    out += struct.pack("<i", 0)

    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(base, "emapp", "test", "fixtures", "test_vertex_morph.pmx")
    with open(path, "wb") as f:
        f.write(out)
    print("wrote %s (%d bytes)" % (path, len(out)))


if __name__ == "__main__":
    main()
