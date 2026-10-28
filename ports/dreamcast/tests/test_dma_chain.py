"""Host and optional SH-4 DMA walker checks (AI-assisted), ISC; see LICENSE."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]


class DmaChainTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'gcc is absent')
    def test_host(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'host_dma_chain.exe'
            subprocess.run([
                shutil.which('gcc'), '-std=c99', '-pedantic', '-Wall', '-Wextra',
                '-Werror', '-O2', str(PORT / 'dma_chain.c'),
                str(PORT / 'host_dma_chain.c'), '-o', str(binary)
            ], check=True)
            run = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertEqual(run.stdout.strip(),
                             'dma_chain: chains, fields, stack, refusals and buckets passed')

    def test_sh4_syntax(self):
        compiler = shutil.which('sh-elf-gcc')
        maintainer = Path('C:/dcw/dc-sdk/sh-elf/bin/sh-elf-gcc.exe')
        if not compiler and maintainer.is_file():
            compiler = str(maintainer)
        if not compiler:
            self.skipTest('sh-elf-gcc is absent')
        subprocess.run([compiler, '-std=c99', '-O2', '-fsyntax-only',
                        '-m4-single', '-ml', str(PORT / 'dma_chain.c')], check=True)


if __name__ == '__main__':
    unittest.main()
