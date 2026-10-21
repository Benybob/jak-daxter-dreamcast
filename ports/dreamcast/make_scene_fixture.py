"""Original synthetic terrain, packaged as GLB to exercise the real converter."""
import json
import math
import pathlib
import struct
import sys


def fixture(path):
    vertices, colors, indices = [], [], []
    for z in range(17):
        for x in range(17):
            px, pz = (x-8)/8, (z-8)/8
            height = 0.42*math.exp(-5*(px*px+pz*pz)) + 0.06*math.sin(x)*math.cos(z)
            vertices.append((px, height, pz))
            colors.append((0.20+height*0.5, 0.45+height*0.45, 0.22, 1.0))
    for z in range(16):
        for x in range(16):
            a = z*17+x
            indices += [a, a+17, a+1, a+1, a+17, a+18]
    payload = b"".join(struct.pack("<fff", *v) for v in vertices)
    color_offset = len(payload)
    payload += b"".join(struct.pack("<ffff", *v) for v in colors)
    index_offset = len(payload)
    payload += struct.pack("<"+"H"*len(indices), *indices)
    doc = {"asset": {"version": "2.0", "generator": "Jak DC synthetic fixture (AI-assisted)"},
           "buffers": [{"byteLength": len(payload)}],
           "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": color_offset},
                           {"buffer": 0, "byteOffset": color_offset, "byteLength": index_offset-color_offset},
                           {"buffer": 0, "byteOffset": index_offset, "byteLength": len(payload)-index_offset}],
           "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(vertices), "type": "VEC3"},
                         {"bufferView": 1, "componentType": 5126, "count": len(colors), "type": "VEC4"},
                         {"bufferView": 2, "componentType": 5123, "count": len(indices), "type": "SCALAR"}],
           "meshes": [{"name": "synthetic-terrain", "primitives": [
               {"attributes": {"POSITION": 0, "COLOR_0": 1}, "indices": 2}]}],
           "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0}
    text = json.dumps(doc).encode()
    text += b" " * (-len(text)%4)
    binary = payload + b"\0" * (-len(payload)%4)
    data = struct.pack("<4sII", b"glTF", 2, 12+8+len(text)+8+len(binary))
    data += struct.pack("<II", len(text), 0x4e4f534a)+text
    data += struct.pack("<II", len(binary), 0x004e4942)+binary
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


if __name__ == "__main__":
    fixture(pathlib.Path(sys.argv[1]))
