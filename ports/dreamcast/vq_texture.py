"""Offline full-codebook VQ for square twiddled ARGB1555 (AI-assisted)."""
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

def payload_size(width,height):
    if width!=height or width<8 or width>512 or width&(width-1):
        raise ValueError('VQ currently requires square power-of-two textures from 8 to 512')
    return (2048+width*height//4+31)&~31

def decode(data,width,height):
    if len(data)!=payload_size(width,height): raise ValueError('invalid VQ payload size')
    return b''.join(data[index*8:index*8+8] for index in data[2048:2048+width*height//4])

def encode(raw,width,height,executable=None):
    size=payload_size(width,height)
    if len(raw)!=width*height*2: raise ValueError('invalid ARGB1555 input size')
    port=Path(__file__).resolve().parent
    executable=Path(executable) if executable else port/'build'/('vq-encode.exe' if sys.platform=='win32' else 'vq-encode')
    if not executable.is_file(): raise ValueError('build the offline VQ encoder with build_host_windows.ps1 or make host')
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory); source=root/'input.bin'; destination=root/'output.bin'
        source.write_bytes(raw)
        subprocess.run([str(executable),str(source),str(destination)],check=True,capture_output=True)
        result=destination.read_bytes()
    if len(result)!=2048+width*height//4: raise ValueError('offline VQ encoder returned an invalid size')
    result+=b'\0'*(size-len(result))
    restored=decode(result,width,height)
    squared=alpha=0
    for (a,),(b,) in zip(struct.iter_unpack('<H',raw),struct.iter_unpack('<H',restored)):
        alpha+=bool((a^b)&0x8000)
        squared+=sum((((a>>shift)&31)-((b>>shift)&31))**2 for shift in (0,5,10))
    if alpha: raise ValueError('VQ alpha pattern changed')
    mse=squared/(width*height*3)
    return result,{'raw_bytes':len(raw),'vq_bytes':len(result),'rgb5_mse':mse,
                   'rgb5_psnr_db':10*math.log10(31*31/mse) if mse else None,
                   'lossless':not squared,'alpha_mismatches':alpha}
