# Native SH-4 module files

2026-10-21. (AI-assisted). DCSH v1 carries code from the original typed GOAL
frontend and native SH-4 backend into the transactional linker. It is separate
from the original PS2 V2/V4 data parser and unsupported PS2 MIPS code files.

`compile_goal_subset.py --runtime --native-module build/module.dcn` emits the
binary alongside its diagnostic C header and provenance manifest. Pass the
usual source, selected functions and header options. The C API
`dc_goal_load_native_module` consumes a borrowed byte buffer.
`dc_goal_load_native_stream` reads exactly one declared span from an open FILE,
leaves the stream owned by the caller and permits an enclosing archive trailer.
It rejects short reads before linking and releases its buffer before returning.
The decoder copies code/private data into the runtime arena, fixes
imports and flushes code caches. Temporary descriptors are freed before return.
No source pointer survives a successful load. The result copies the initializer
name and records its ABI; invocation is explicit through the uint64 or void
initializer entry point. File startup currently rejects floating return ABIs.

The little-endian header is 64 bytes: magic DCSH followed by fifteen uint32
words: version=1, ABI=1, total bytes, function count, data count, relocation
count, string bytes, payload bytes, initializer string offset, initializer kind,
body CRC32 and four reserved zero words. The absent initializer offset is
UINT32_MAX. Kinds are 0 absent, 1 uint64, 2 void. ABI 1 is GCC SH-4 little-endian
`-m4-single` with the scalar uint64 container convention described in RUNTIME.md.

Function records are six uint32 words: name offset, payload offset, code bytes,
first relocation, relocation count, reserved zero. Private data records replace
the last three fields with address offset, first relocation, relocation count.
Relocations are pairs of uint32 words: patch offset and import-name offset.
Records precede a NUL-terminated string table and payload. Each payload object
starts at a four-byte relative offset. Relocation ranges must be contiguous and
exhaust the relocation table. Import resolution and patch alignment use the
existing shared linker validation; linking failures restore symbols and arena.

Limits: 2 MiB file, 256 functions, 256 data objects, 16,384 total relocations,
1,024 relocations per object, 64 KiB strings and 1 MiB payload. Header arithmetic
is bounded before allocation. CRC32 detects accidental corruption; it is not
authentication or a sandbox. Only trusted native compiler output is executable.
The startup ABI is the compiler/caller contract, not machine-code verification.
Startup side effects do not roll back. Module unload, process heaps, general
mixed/vector ABIs, scheduling and complete original kernel boot remain open.

The 1,637-byte native fixture carries four functions, a void initializer, a
string and a quoted pair graph. Flycast frees the input buffer before running
code and checks private-data stability, globals, explicit startup, mutation,
reinitialization and corrupt-file rejection. The native KOS `/ram` test writes
an enclosing prefix/module/trailer, checks short-read rollback, loads the exact
module span, reads the untouched trailer and closes/removes the file before
executing code. This adds 50 checks to the runtime suite, reaching
**7,982/7,982** on stock 16 MiB RAM. **98 host tests** include
real file read/decode/link, malformed headers/records, CRC failures, private
data and missing-import rollback; the host never executes SH-4 code.

The diagnostic reports 22,866 emitted bytes (including a deliberately retained
3,244-byte shift baseline), 296,544 / 1,048,576 arena bytes and 209 installed
symbols. Decoder metadata and the linker's temporary symbol snapshot are
additional transient allocations; these figures exclude the executable, KOS,
stack and renderer and do not establish a complete game memory budget.
