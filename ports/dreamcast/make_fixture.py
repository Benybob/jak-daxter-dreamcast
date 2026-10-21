"""Generate only original synthetic data, using the DGO container layout."""
import pathlib
import struct
import sys


def archive(entries, name=b"DC-PROBE"):
    result = bytearray(struct.pack("<I60s", len(entries), name))
    for label, payload in entries:
        result += struct.pack("<I60s", len(payload), label)
        result += payload
        result += b"\0" * (-len(payload) % 16)
    return bytes(result)


if __name__ == "__main__":
    path = pathlib.Path(sys.argv[1])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(archive([(b"probe-text", b"Dreamcast DGO streaming probe\n"),
                              (b"crc-vector", b"123456789")]))
