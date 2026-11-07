"""Synthetic checks of the skinned model bundle (make_skin_models.py) (AI-assisted), ISC."""

import base64
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    import numpy  # noqa: F401
    import PIL  # noqa: F401
    import make_skin_models as msm
except ImportError:  # the converter needs numpy and Pillow
    msm = None


def png(width, height, rgba=(0x12, 0x34, 0x56, 0xff)):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    pixels = (b"\0" + bytes(rgba) * width) * height
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(pixels))
            + chunk(b"IEND", b""))


def glb(doc, binary):
    text = json.dumps(doc).encode()
    text += b" " * (-len(text) % 4)
    binary += b"\0" * (-len(binary) % 4)
    chunks = struct.pack("<I4s", len(text), b"JSON") + text + struct.pack("<I4s", len(binary), b"BIN\0") + binary
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def skinned(joints_of_vertex3=(2, 0, 0, 0), extra_mesh=False):
    """A quad (shared corner vertices across two primitives) skinned to joints 2 and 3."""
    binary = bytearray()
    views, accessors = [], []

    def add(data, component, kind, count):
        views.append(dict(buffer=0, byteOffset=len(binary), byteLength=len(data)))
        binary.extend(data)
        binary.extend(b"\0" * (-len(binary) % 4))
        accessors.append(dict(bufferView=len(views)-1, componentType=component, type=kind, count=count))
        return len(accessors)-1
    pos = add(struct.pack("<12f", 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0), 5126, "VEC3", 4)
    uv = add(struct.pack("<8f", 0, 0, 1, 0, 1, 1, 0, 1), 5126, "VEC2", 4)
    joints = add(struct.pack("<16H", 2, 3, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, *joints_of_vertex3), 5123, "VEC4", 4)
    weights = add(struct.pack("<16f", 0.75, 0.25, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0), 5126, "VEC4", 4)
    first = add(struct.pack("<3H", 0, 1, 2), 5123, "SCALAR", 3)
    second = add(struct.pack("<3H", 0, 2, 3), 5123, "SCALAR", 3)
    ibm = []
    for j in range(4):  # column-major translations (0, 0, -j) in meters
        ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.0, 0.0, -float(j), 1]
    ibms = add(struct.pack("<64f", *ibm), 5126, "MAT4", 4)
    attrs = dict(POSITION=pos, TEXCOORD_0=uv, JOINTS_0=joints, WEIGHTS_0=weights)
    image = "data:image/png;base64," + base64.b64encode(png(16, 16)).decode()
    nodes = [dict(name="align"), dict(name="prejoint", children=[2]), dict(name="main", children=[3]),
             dict(name="arm"), dict(name="model", mesh=0, skin=0)]
    if extra_mesh:
        nodes.append(dict(name="other", mesh=0))
    doc = dict(asset=dict(version="2.0"), buffers=[dict(byteLength=len(binary))], bufferViews=views,
               accessors=accessors, nodes=nodes, skins=[dict(joints=[0, 1, 2, 3], inverseBindMatrices=ibms)],
               meshes=[dict(primitives=[dict(attributes=attrs, indices=first, material=0),
                                        dict(attributes=attrs, indices=second, material=1)])],
               images=[dict(uri=image)], samplers=[{}], textures=[dict(source=0, sampler=0)],
               materials=[dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0))),
                          dict(alphaMode="MASK", pbrMetallicRoughness=dict(baseColorFactor=[1, 0.5, 1, 1]))])
    return glb(doc, bytes(binary))


@unittest.skipIf(msm is None, "make_skin_models needs numpy and Pillow")
class SkinModelTest(unittest.TestCase):
    def write(self, folder, name, data):
        path = Path(folder)/(name+".glb")
        path.write_bytes(data)
        return path

    def test_bundle_layout(self):
        with tempfile.TemporaryDirectory() as folder:
            data, report = msm.bundle([self.write(folder, "quad-lod0", skinned())])
        magic, version, count, _ = struct.unpack_from("<4s3I", data, 0)
        self.assertEqual((magic, version, count), (b"DSKB", 1, 1))
        name, offset, size, crc, _ = struct.unpack_from("<32s4I", data, 16)
        self.assertEqual(name.rstrip(b"\0"), b"quad-lod0")
        self.assertEqual(offset % 32, 0)
        blob = data[offset:offset+size]
        self.assertEqual(zlib.crc32(blob), crc)
        head = struct.unpack_from("<4s7I4f", blob, 0)
        magic, version, joints, verts, indices, batches, textures, tbytes = head[:8]
        centre, scale = head[8:11], head[11]
        self.assertEqual((magic, version, joints, indices, batches, textures), (b"DSK1", 1, 4, 6, 2, 1))
        self.assertEqual(verts, 6)  # corners split by their colour factor across the two materials
        self.assertEqual(tbytes, 16*16*2)
        # inverse bind matrices in row-vector layout: identity rows, then the translation
        rows = struct.unpack_from("<48f", blob, 48)
        self.assertEqual(rows[12*3:12*3+12], (1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, -3))
        at = 48+4*48
        records = [struct.unpack_from("<5h3B2BBI", blob, at+20*k) for k in range(verts)]
        first = records[0]
        x, y, z = (centre[i]+first[i]*scale for i in range(3))
        self.assertAlmostEqual(x, 0, places=4)
        self.assertAlmostEqual(y, 0, places=4)
        self.assertEqual(first[5:8], (2, 3, 2))  # strongest first; an unused slot repeats it
        self.assertEqual(first[8]+first[9], 255)  # 0.75 and 0.25 of 255, the third slot empty
        self.assertEqual(first[8], 191)
        self.assertEqual(first[3:5], (0, 0))
        for record in records:
            self.assertLessEqual(record[8]+record[9], 255)
        at += 20*verts
        index = struct.unpack_from("<6H", blob, at)
        self.assertTrue(all(i < verts for i in index))
        at += 12
        batch = [struct.unpack_from("<IIHH", blob, at+12*k) for k in range(2)]
        self.assertEqual(batch[0], (0, 3, 0, 0))
        self.assertEqual(batch[1], (3, 3, 0xffff, 1))  # MASK without a texture: punch-through, untextured
        at += 24
        width, height, fmt, size = struct.unpack_from("<HHII", blob, at)
        self.assertEqual((width, height, fmt, size), (16, 16, 1, 512))
        self.assertEqual(report["quad-lod0"]["triangles"], 2)

    def test_unweighted_joint_slots_may_name_any_joint(self):
        with tempfile.TemporaryDirectory() as folder:
            msm.bundle([self.write(folder, "quad-lod0", skinned(joints_of_vertex3=(2, 99, 99, 99)))])

    def test_weighted_joint_outside_the_skin_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.write(folder, "quad-lod0", skinned(joints_of_vertex3=(9, 0, 0, 0)))
            with self.assertRaisesRegex(ValueError, "joint index outside the skin"):
                msm.bundle([path])

    def test_one_skinned_mesh_required(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.write(folder, "quad-lod0", skinned(extra_mesh=True))
            with self.assertRaisesRegex(ValueError, "exactly one skinned mesh"):
                msm.bundle([path])

    def test_long_names_are_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.write(folder, "x"*32, skinned())
            with self.assertRaisesRegex(ValueError, "longer than 31"):
                msm.bundle([path])


if __name__ == "__main__":
    unittest.main()
