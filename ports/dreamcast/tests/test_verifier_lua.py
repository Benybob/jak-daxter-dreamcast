"""Regression for overlapping target-symbol names in Lua (AI-assisted)."""
from pathlib import Path
import sys
import unittest
PORT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PORT))
from verify_training import LUA,render_lua

class LuaSymbols(unittest.TestCase):
    def test_overlapping_symbols_and_inserted_paths_are_not_replaced(self):
        out='"C:/SCOUT/CLIP/CLIPPED"'
        self.assertEqual(render_lua('CLIP CLIPPED OUT PLAYER',dict(CLIP='101',CLIPPED='202',OUT=out,PLAYER='303')),
            '101 202 '+out+' 303')

    def test_actual_training_script_uses_two_independent_addresses(self):
        values=dict(PLATFORM='false',ADDRESS='1000',PLAYER='2000',CLIP='3000',TICKS='4000',
            CLIPPED='5000',RESPAWNS='6000',OUT='"/private/output"')
        script=render_lua(LUA,values)
        self.assertIn('read8(3000+i)',script)
        self.assertIn('read8(5000+i)',script)
        self.assertNotIn('3000PED',script)
        self.assertIn('read32(4000)',script)
