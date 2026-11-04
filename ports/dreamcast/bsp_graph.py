"""Object graph of a retail v4 BSP object (AI-assisted), ISC.

Splits the data into objects at 16-aligned basic tags. A piece that nothing
points into (inline-array elements, an array's content-type tag) belongs to
the object before it. Edges are the pointer relocations. Used to measure and
to build a logic-only BSP; it reads the owner's DGO and writes nothing.
"""
import bisect
import collections
import struct

from stage_jak_art import decode_v4, u32

TREES_OFFSET = 40       # bsp-header drawable-trees (retail, from the tag)
GROUP_LENGTH = 6        # drawable-group length, int16
GROUP_DATA = 32         # drawable-group data[0]


class BspGraph:
    def __init__(self, obj):
        self.data, self.pointers, self.bindings = decode_v4(obj, 'bsp-header')
        data = self.data
        self.tags = {at: name for name, methods, sites in self.bindings if methods is not None for at in sites}
        targets = sorted({u32(data, at) for at in self.pointers})
        candidates = sorted(at for at in self.tags if at % 16 == 0)
        # Keep a split only where something points into the piece it starts.
        starts = [0]
        for i, at in enumerate(candidates):
            if at == 0:
                continue
            end = candidates[i + 1] if i + 1 < len(candidates) else len(data)
            k = bisect.bisect_left(targets, at)
            if k < len(targets) and targets[k] < end:
                starts.append(at)
        self.starts = starts
        self.ends = starts[1:] + [len(data)]
        self.edges = collections.defaultdict(set)
        for at in self.pointers:
            self.edges[self.region(at)].add(self.region(u32(data, at)))

    def region(self, offset):
        return bisect.bisect_right(self.starts, offset) - 1

    def size(self, r):
        return self.ends[r] - self.starts[r]

    def name(self, r):
        return self.tags.get(self.starts[r], '?')

    def trees(self):
        array = u32(self.data, TREES_OFFSET) - 4
        n = struct.unpack_from('<h', self.data, array + GROUP_LENGTH)[0]
        slots = [array + GROUP_DATA + 4 * i for i in range(n)]
        return array, [(slot, u32(self.data, slot) - 4, self.tags.get(u32(self.data, slot) - 4)) for slot in slots]

    def reach(self, roots, blocked=frozenset()):
        """Regions reachable from roots without entering `blocked` regions."""
        seen, stack = set(), list(roots)
        while stack:
            r = stack.pop()
            if r in seen or r in blocked:
                continue
            seen.add(r)
            stack.extend(self.edges[r])
        return seen
