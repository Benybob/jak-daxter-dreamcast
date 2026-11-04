(AI-assisted)

# Jak art-load symbol type-word failure

This note is a static reading of the Flycast evidence and the source on that path. Nothing here was executed.

## What the dump actually proves

`ports/dreamcast/build/jak-corruption-evidence.txt` records one boot of `jak-dc-arena-jak.elf`. Stage 0 is `dc_arena_jak_exports(rt, 0)` in `arena_jak.c` before art load: four exports authenticate, `used` is 10,681,536 of 12,124,096, code-registry head is `0xcbe2680`, count is 4597. Stage 1 is `dc_arena_jak_exports(rt, 1)` at `arena_jak_player.c:116`. All four exports fail `DC_CODE_RECORD_SYMBOL` (reason 7) at chain index 0. The record is unchanged: `0xcbe2680`, entry `0xcbe2674`, bytes 12, symbol `0xc1b5b5c`, next `0xcbde620`, same head and count. `used` is 12,000,832. Control reached `arena_jak_player.c:122` (why 13), so the art-load `REQUIRE` at `arena_jak.c:128`, the post-lookup art-group `REQUIRE` at `arena_jak.c:131`, and the process-span `CHECK` at `arena_jak_player.c:112` all passed.

`dc_goal_code_entry_diagnose` (`goal_module.c:50-51`) refuses when `dc_goal_type_of(rt, record.symbol) != rt->symbol_type` and stops at the first bad record. `dc_goal_type_of` (`goal_runtime.c:284-296`) reads the word at `value-4` and returns it only when `type_fields` (`goal_runtime.c:116-127`) accepts it: alignment `== 4` mod 16, tag at `type-4` equal to `rt->type_type`, and `num_methods` in `1..126`. On failure it returns 0 and sets `rt->error`. The receipt stores the symbol address (`goal_module.c:43`), not the word at `0xc1b5b58` and not the symbol-type object. Two GOAL states produce this receipt:

1. The word at `0x0C1B5B58` changed to something other than `rt->symbol_type`.
2. That word is still `rt->symbol_type`, but the symbol-type object no longer passes `type_fields`, so every symbol fails and the walk reports the head first.

The head symbol can be the only damaged word or the first of many. Neighbors were not read.

Addresses are cached P0 (`goal_runtime_dc.c:8-19`). `0xc1b5b5c` is `0x0C1B5B5C`. The symbol value cell is that address (`≡ 4` mod 8). Its type word is `0x0C1B5B58` (`≡ 0` mod 8). The previous symbol's value is `0x0C1B5B54`. The next symbol's type word is `0x0C1B5B60`.

## Where the victim sits

`dc_goal_init` (`goal_runtime.c:474-479`) requires a 16-byte-aligned base and makes the symbol table the first bump allocation, 16384 × 16 = 262144 bytes. `table == base`. `s7` is `table + 0x10004`. Fixed symbols live at `s7 + 0 .. s7 + 0x1d0` (`goal_layout.h:74`), table offsets `0x10004..0x101D4`. Region 1, the `(type, value)` pairs, is `[table, table+0x20000)`. Hash and name pointers live at `symbol + (MAX*8 - 4)` and `symbol + MAX*8` (`goal_layout.h:8-9`), which for every in-table symbol is at table offset `>= 0x20000`.

Stage-0 `used` is `0xA2FCC0`. The 16-byte head record is inside that span, so

`base >= 0x0CBE2690 - 0xA2FCC0 = 0x0C1B29D0`

and the symbol offset `0x0C1B5B5C - base` is at most `0x318C`. The offset is `≡ 12` (mod 16), so the slot `(offset - 4) / 8` is in `1..1585`. `0x318C < 0x10004`, so this symbol is in the low half, below `s7`, and is not a fixed symbol. The record is `0xA2CB24` = 10,668,836 bytes above the symbol. The art bump starts at `base + 0xA2FCC0`, which is at least `0xA2CB34` bytes above the type word and at or after the head record. Later string and login allocations extend that bump further upward.

`used` grew by `12,000,832 - 10,681,536 = 1,319,296`. The eichar payload is 1,318,288 bytes (`jak_eichar_check.c`, `ARENA_JAK_NOTES.md`). The extra 1,008 bytes are new symbol-name strings and any GOAL allocation inside login. The host linker test (`jak_art_check.c:59-73`) accounts that string tail separately from the payload (65 names and 2,000 bytes when none are preinterned). A 1,008-byte tail is the same class of allocation with fewer new names. It is not a moved symbol table.

## Path that runs between the two checks

`arena_jak.c:123` takes the stage-0 snapshot. Then:

- `dc_arena_jak_art_load` (`arena_jak_art.c:43`) admits `EICHAR.DGO` (free bytes are under `DC_JAK_ART_BYTES + 65536`) and calls `dc_goal_load_art_dgo` (`goal_art.c:131`).
- That loader validates the retail V4/V2 link tables with `data == NULL` (no intern, no stores), bump-allocates the payload once, copies the file in, applies links, then calls the relocate callback once per object.
- The callback (`arena_jak_art.c:23-42`) saves `*level*` loading-level at `levels+8`, writes the default level, and calls original `(method relocate art-group)` as `(art, value of global, word(art+4)+4)`.
- `joint.gc:683-699` copies the char* into `*temp-string*`. The C wrapper left loading-level non-null and named `default`, so both `(login this)` (`joint.gc:696`) and `set-loaded-art` (`joint.gc:697`) run. `art-group` login (`joint.gc:667-671`) calls `login` on each element: `art` (`joint.gc:566-571`), `art-joint-anim` (`joint.gc:714-717`), and `merc-ctrl` (`merc.gc:165-202`), which calls `adgif-shader-login`. `*debug-segment*` is `#f` (`arena_jak.c:105`), so startup `texture.gc:2062-2064` has installed `adgif-shader-login-fast`.
- `arena_jak.c:129-131` calls original `load-to-heap-by-name` with `do-reload` = `#f`. On a hit, `loader.gc:59-63` returns the registered art and does not store. With `*debug-segment*` false, a miss does not call `loado` (`loader.gc:114-120`). This call returned a typed `art-group`.
- `dc_arena_jak_player_run` (`arena_jak_player.c:102-115`) sets `phase` and queries process spans (`process_spans.c:80` reads registration state). The next GOAL-facing call is the stage-1 export walk.

`arena_jak_dgo.c` only recompiles `goal_dgo.c` so boot initializers go through `dc_arena_jak_initializer`. `goal_art.c` does not call it. `goal_module.c` linking is not on this path: the registry count stayed 4597, and a rolled-back `dc_goal_load_module_data` would have restored `used` (`goal_module.c:293-298`). `goal_object_load.c` is a separate host/data loader; the device art path does not call it.

`word(art+4)+4` is the character pointer of the art-group name, not a bug. Decompiler offsets count the type tag as offset 0 (`all-types.gc` puts `process-tree.name` at 4 and `art.name` at 8). The C object pointer is the allocation plus 4, so the name is at object+4. `jak_art_check.c:18-22` requires `word(art+4)` to be the GOAL string `"eichar"` / `"sidekick"`. `string<-charp` (`gstring.gc:128-137`) copies that short, null-terminated name into `*temp-string*` (256 bytes, `gstring.gc:550`).

## Ranked writers

### 1. Original GOAL store inside `art-group.relocate`

Call site: `arena_jak_art.c:37`, reached from `goal_art.c:193`. This is the only code in the window that can form an address from a pointer or an index instead of writing at `bump + offset`.

The stores that leave the art object, and the address each one actually uses when its base is the pointer the linker or the pre-window checks established:

| Site | Store | Destination when the base is intact |
| --- | --- | --- |
| `gstring.gc:132-136` | byte copy until a null | `*temp-string*` data, forward, after the symbol table |
| `joint.gc:570`, `joint.gc:716` | `extra.tag` at res-lump offset 24 | inside the art object; `extra` is an internal link |
| `merc.gc:168` | `(set! *merc-ctrl-header* header)` | 4-byte symbol value cell. `set_symbol` lowers to one 32-bit store (`lower_sh4_ir.py:1486-1488`). A value cell is `≡ 4` (mod 8). `0x0C1B5B58` is `≡ 0` (mod 8), so this store does not land on a type word |
| `merc.gc:170-171`, `merc.gc:63`, `merc.gc:91`, `merc.gc:200-201` | mask clears and `logior!` | the inline header just stored at `merc.gc:168` (`merc-h.gc:241`, `header` is `:inline`) |
| `merc.gc:37-57` | `tex0` / `clamp` field stores, including 64-bit GS fields | the shader cursor inside the fragment |
| `merc.gc:96-101` | advance `frag-geo` / `frag-ctrl` by `asize-of` | no store of its own; the next `login-adgifs` writes at the advanced pointer |
| `texture.gc:2030-2037` | bounded index into `*texture-page-dir*` entry `link` | the page's link array, allocated after the symbol table. The index must be `<` the entry length and the page `<` the directory length |
| `loader.gc:80-83` | append name and art at the current array length | `array + 12 + length*4`. Before the window, `arena_jak.c:102-105` has both lengths `<= 50` and `<=` allocated-length. A full directory stores one slot past the array, still next to that level object |
| `joint.gc:670` | `(set! (-> this data i) (login ...))` | `art-group` data at decompiler offset 32, inside the art object. A runaway `length` writes forward from the highest allocation. Reaching `0x0C1B5B58` that way wraps through the top of the 32-bit space. That pass would also destroy the art-group object and the code record. Both survived |

None of those intact bases is `0x0C1B5B58`. The art object, `*temp-string*`, the level arrays, and the texture link arrays are all allocated after the symbol table, and their in-range stores move forward. The store that hits `0x0C1B5B58` is one of these same GOAL stores whose base is already a symbol-table address, or an 8-byte memory store whose address is the previous symbol's value. `store_memory` of size 8 writes two words (`lower_sh4_ir.py:1546-1552`). A store at `0x0C1B5B54` covers `0x0C1B5B58`. A 4-byte store at offset -4 from the symbol `0x0C1B5B5C` is that type word exactly (the shape of a type-tag store aimed at a symbol pointer). `tex0` updates in `merc.gc:37-43` are the 8-byte-wide stores on this path; they hit the symbol table only when the shader pointer is already a low address. `merc.gc:89` and `merc.gc:96` chase `frag-geo`, `shader-offset`, and `eye-ctrl`. A symbol import left in one of those fields is a low pointer treated as structure.

The replacement word is not in the dump, so this does not name one instruction. Rank 1 is the class: an addressed store in original relocate/login, not a C memcpy on the art path.

### 2. Same GOAL window, aimed at the symbol-type object

`rt->symbol_type` is a type object allocated immediately after the symbol table (`goal_runtime.c:482-491`), on the order of `base + 0x40000`, still about 6.5 MB below the art bump. If a login store zeroes that object's tag or its `num_methods` at object+14 (`goal_layout.h:79-82`), `type_of` returns 0 for every symbol and `goal_module.c:50` reports the head. The dump cannot tell this from rank 1. It is the same class of addressed store, ranked second because the direct reading of reason 7 is the word at `symbol-4`.

### 3. Rejected: art-link relocation with a wrong base or size

`goal_art.c:47` `put` writes 4 bytes at `data + at`. `site` (`goal_art.c:42`) requires `at <= size - 4` and 4-byte alignment. Pointer runs (`goal_art.c:70-72`) require the stored offset `< size` and write `base + offset` into that slot. Symbol and type runs (`goal_art.c:117-119`) require a `0xFFFFFFFF` placeholder and write either the symbol pointer or the existing type object, again only at `data + at`. `base` is the `dc_goal_alloc` result at the stage-0 bump (`goal_art.c:177`), in the `0xcbe...` range, and `base + offset` stays inside the art object. The validate pass (`goal_art.c:172`) uses `data == NULL` and performs no stores. A bad offset fails the load. The load returned success, and the second lookup still saw an `art-group`. These writes cannot address `0x0C1B5B58`.

The host comparison in `ARENA_JAK_NOTES.md` checks the linked bytes against an independent decoder. That test's login callback is a double (`jak_art_check.c:16-23`). It does not run `joint.gc` login.

### 4. Rejected: symbol table grows, rehashes, or moves

The table is one allocation of `DC_GOAL_SYMBOL_BYTES` (`goal_runtime.c:477`). `dc_goal_intern` (`goal_runtime.c:268-276`) returns an occupied slot without writing. `find_slot` (`goal_runtime.c:212-243`) walks by 8 from addresses `≡ 4` (mod 8) and treats hash 0 as empty. Nothing reallocates or rehashes the table. Old symbol pointers stay valid. The registry still holds `0xc1b5b5c`.

### 5. Rejected: intern writes a non-type value at `0x0C1B5B58`

`set_slot` (`goal_runtime.c:187-195`) writes `rt->symbol_type` at `symbol-4`, the value at `symbol`, the hash at `symbol+131068`, and the string pointer at `symbol+131072`. Writing `symbol_type` at the victim would make reason 7 pass. The hash and string slots of every in-table symbol are at table offset `>= 0x20000`. This victim's type word is at table offset `<= 0x3188`. They do not alias. `find_slot` never returns an address `≡ 0` (mod 8), so the off-by-4 "pass `S-4` into `set_slot`" path is not this intern. `"_empty_"` returns `s7-10` (`goal_runtime.c:220`) and does not call `set_slot`. Art linking does not call `dc_goal_intern_type`; a missing type fails the apply pass (`goal_art.c:96`).

### 6. Rejected: a link that treats file data as a pointer, inside the C linker

The C linker never stores through a relocated word. It stores into `data+at` and puts either `base+offset`, a symbol pointer, or a type object there (`goal_art.c:71` and `goal_art.c:119`). Chasing a symbol pointer as a structure is rank 1, and it happens in GOAL login after the link has finished.

### 7. Rejected: bump allocation overlaps the symbol region

`dc_goal_alloc` (`goal_runtime.c:56-58`) `memset`s `[used, used+size)` and advances `used`. The art block starts at stage-0 `used`, at least `0xA2CB34` bytes above `0x0C1B5B58`, for `1,318,288` bytes rounded up to 16. The following 1,008 bytes continue upward. `memset` does not run backward into region 1.

### 8. Rejected: off-by-4 between symbol and value, in C or in `set_symbol`

`dc_goal_set_symbol` (`goal_runtime.c:144-146`) writes the value cell only, after `symbol_address` has required the type word to be `symbol_type`. The SH4 lowering of `set_symbol` is a 32-bit store (`lower_sh4_ir.py:1488`). A 4-byte store at `0x0C1B5B54` or at `0x0C1B5B5C` does not touch `0x0C1B5B58`. The 8-byte case that does is rank 1, and it is `store_memory`, not a symbol-cell store. Kernel and `GAME.CGO` had already run before stage 0 with this type word intact, so symbol stores are not uniformly 8 bytes.

### 9. Rejected: module relocation and the C wrapper's own stores

`goal_module.c:250-267` copies 4-byte import addresses into the function or data object. Art load does not call it, and the registry count did not change. The wrapper's only GOAL stores are the 4-byte save/restore of `*level*+8` (`arena_jak_art.c:36-39`). That object was checked as a `level-group` before the window (`arena_jak.c:93-94`) and lives after the symbol table. `0x0C1B5B58` is inside region 1 of the table, which is never reused for a level object.

### 10. Rejected: the second lookup and `dc_arena_jak_player_run` up to the check

`loader.gc:59-63` returns the existing art when `do-reload` is `#f`. `arena_jak_player.c:102-115` does not write GOAL memory. The stage-1 walk observes the damage; `type_of` can set `rt->error` while it refuses, and that is after the word has already failed.

## Smallest one-run probe

Add a sampler that reads GOAL memory directly. It must not call `dc_goal_type_of` or `dc_goal_find` (both can change `rt->error`) and must not allocate. It follows the head record's symbol, so a moved heap base still names the symbol the checker fails, and it records the address so this ELF can be checked against `0x0c1b5b5c`.

In `goal_art.h`, after the `dc_goal_load_art_dgo` declaration (line 20):

```c
void dc_goal_art_probe(dc_goal_runtime *rt, uint32_t step);
```

In `goal_art.c`, after the includes (line 4):

```c
typedef struct {
  uint32_t step, used, symbol, type_word, prev_value, next_type;
  uint32_t symbol_type, symbol_type_tag, symbol_type_methods;
} dc_goal_art_probe_row;
volatile struct {
  uint32_t version, count;
  dc_goal_art_probe_row row[8];
} dc_goal_art_symbol_probe = {.version = 1};

static uint32_t probe_mem(dc_goal_runtime *rt, uint32_t address, void *out, uint32_t n) {
  uint32_t offset;
  if (!rt || !rt->memory || address < rt->base) return 0;
  offset = address - rt->base;
  if (offset > rt->used || n > rt->used - offset) return 0;
  memcpy(out, rt->memory + offset, n);
  return 1;
}
void dc_goal_art_probe(dc_goal_runtime *rt, uint32_t step) {
  dc_goal_art_probe_row *row;
  uint32_t symbol = 0, word = 0xffffffffu, half = 0xffffu;
  if (dc_goal_art_symbol_probe.count >= 8) return;
  row = &dc_goal_art_symbol_probe.row[dc_goal_art_symbol_probe.count++];
  memset(row, 0xff, sizeof *row);
  row->step = step;
  row->used = rt ? rt->used : 0;
  row->symbol_type = rt ? rt->symbol_type : 0;
  if (rt && probe_mem(rt, rt->code_registry_head + 8u, &symbol, 4)) row->symbol = symbol;
  if (symbol > 16u) {
    if (probe_mem(rt, symbol - 4u, &word, 4)) row->type_word = word;
    if (probe_mem(rt, symbol - 8u, &word, 4)) row->prev_value = word;
    if (probe_mem(rt, symbol + 4u, &word, 4)) row->next_type = word;
  }
  if (row->symbol_type > 16u) {
    if (probe_mem(rt, row->symbol_type - 4u, &word, 4)) row->symbol_type_tag = word;
    if (probe_mem(rt, row->symbol_type + 14u, &half, 2)) row->symbol_type_methods = half;
  }
}
```

Call `dc_goal_art_probe(rt, N)` at these points:

| `N` | Where | What just finished |
| --- | --- | --- |
| 1 | `goal_art.c:142`, after the `rt->error` check | loader entry, before any art write |
| 2 | `goal_art.c:176`, after the validate `links` loop | link tables accepted, still no intern and no payload |
| 3 | `goal_art.c:177`, after `base = dc_goal_alloc(rt, total)` | payload zeroed at the top of the bump |
| 4 | `goal_art.c:186`, after `result->art[i] = base + 4` | intern plus `put` for that object |
| 5 | `goal_art.c:193`, after `++result->relocated` | original relocate/login/set-loaded-art returned |
| 6 | `arena_jak.c:128`, after `dc_arena_jak_art_load` returns | C wrapper finished, loading-level restored |
| 7 | `arena_jak.c:130`, after the second lookup | `load-to-heap-by-name` returned |
| 8 | `arena_jak_player.c:111`, after `phase = 1` | nothing GOAL-visible yet in `player_run` |

`arena_jak.c` and `arena_jak_player.c` need `goal_art.h` if they do not already include it. Dump the volatile symbol `dc_goal_art_symbol_probe` the same way `dc_arena_jak_exports_evidence` was dumped. `count` is the number of stored rows.

How to read one dump:

- The first row whose `type_word != symbol_type` is the first step after the store. Rank 1 predicts row 4 still equal and row 5 unequal. A change at row 3 or 4 would refute that and implicate intern or `put`, which the address math says cannot form this address; the recorded word is then the evidence.
- `type_word` still equal to `symbol_type`, but `symbol_type_tag` not equal to `rt->type_type` or `symbol_type_methods` outside `1..126`: rank 2. The symbol-type object was damaged and the head record is only the first checked.
- `prev_value` and `type_word` both changed on the same step: an 8-byte store at `0x0C1B5B54` (`lower_sh4_ir.py:1552`). Only `type_word` changed: a 4-byte store at `0x0C1B5B58`.
- `next_type` changed as well: the smash is wider than this one type word. The walk never looked at the neighbors.
- `used` at row 4 versus row 5 splits the 1,008-byte tail into symbol strings and login allocation.

## Fix

No source change is justified from this dump. The C linker, intern, bump, module relocator, and symbol-cell store cannot place a non-`symbol_type` value at `0x0C1B5B58`, and the captured receipt does not contain the replacement word or the step. Changing `arena_jak_art.c:37`, the intern layout, or the link bounds would not be a fix supported by these addresses. The probe above is the next edit. If row 5 is the first bad row, the follow-up is to split that one callback (after `string<-charp`, after `art-group` login, after `set-loaded-art`), still without calling `dc_goal_find` or `dc_goal_type_of`.

## Probe result and culprit

The probe confirms rank 2. Through step 4 the `symbol` type object at `0x0C1F3054` still has method count 9. Step 5, the original art-group relocate/login, is the step that changes the halfword at `symbol_type+14` from 9 to 52201 (`0xCBE9`). The head symbol's type word stays `0x0C1F3054`, and the type tag at `symbol_type-4` stays `0x0C1F3014` (`type`). `used` is the same on steps 4 and 5 (`12000832`), so login did not allocate. `type_fields` (`goal_runtime.c:123-124`) then rejects `num_methods` outside `1..126`, `dc_goal_type_of` returns 0 for every symbol, and the export walk reports the head record.

`0xCBE9` is the shader-pointer packing of an eichar address, not the high half of a raw GOAL pointer. `dc_goal_init` (`goal_runtime.c:477-484`) places the symbol table at `base` and the builtin types immediately after it, 64 bytes apart. The probe's `symbol_type` `0x0C1F3054` and its tag `0x0C1F3014` are 64 bytes apart, so `base = 0x0C1F3054 - 0x40104 = 0x0C1B2F50` and `s7 = base + 0x10004 = 0x0C1C2F54`. The art block begins at `base + 0xA2FCC0 = 0x0CBE2C10`. A raw store of a pointer in `0x0CBE9xxx` would leave `0x0CBE` (3278) in the half at `+14`. The measured half is `0xCBE9`.

### The store

`texture.gc:2037`, in `adgif-shader-login-fast`:

```goal
(set! (-> dir-entry link next (-> tex-id index) shader) (shr (the-as uint shader) 4))
```

`*debug-segment*` is `#f`, so `texture.gc:2062-2064` has installed this function as `adgif-shader-login`. Art-group relocate (`joint.gc:696`) calls `login`, which reaches it from `merc-ctrl` login (`merc.gc:174` to `merc.gc:99` to `merc.gc:60`, and the eye path at `merc.gc:198`). `level-remap-texture` (`bsp.gc:445`) reads `*level*` `log-in-level-bsp`, which the static level-group leaves as `#f` (`level-h.gc:252`), so the texture id is the one in the art file.

The code expects `dir-entry` to be a `texture-page-dir-entry` (`texture-h.gc:174-178`: `length` at 0, `link` at 8, packed stride 12, confirmed by `texture.gc:1715`) and `link` to be that page's `texture-link` (`next` is a `shader-ptr` array at 0). It gets the `symbol` type object. `*texture-page-dir*` is still the `#f` from `texture-h.gc:212`. Nothing in this boot loads `dir-tpages`, which is the only assignment (`texture.gc:1725`, from `texture-page-dir.relocate`).

`#f` is the symbol at `s7`. Treating it as a `texture-page-dir` (`texture-h.gc:183-185`, type-system size `0x14`):

- `(-> #f length)` is the word at `s7+0`, the value of `#f`, which is `s7` itself (`goal_runtime.c:495`). Every page id passes the bound at `texture.gc:2030`.
- Entry `page` is at `s7 + 4 + page*12`. Its `link` word is at `s7 + 12*(page+1)`.
- For every even `page`, that address is 8-byte aligned, so it is a symbol type tag. In the fixed-symbol block (`s7` through `s7+0x1d0`, and any later interned slot) that tag is `rt->symbol_type`. Page 2 is the first one: `s7+36 = 0x0C1C2F78`, the type word of the fixed symbol `symbol` at `s7+0x28`.
- The entry `length` is the low half of that same tag, `0x3054` = 12372, so any real texture index passes `texture.gc:2034`. The link pointer is nonzero, so the `when` body runs.

`link` is therefore the `symbol` type object. `(-> link next index)` is `symbol_type + index*4`. Index 3 stores at `symbol_type+12` (`0x0C1F3060`).

`shader-ptr` (`texture-h.gc:164-165`) is a `uint32` whose `shader` field is 24 bits at bit 8. `set_bitfield` (`goalc/compiler/compilation/Define.cpp:178-194`) loads that word, inserts the value, and stores the word back. The source value is already `(shr shader 4)`, so bits 8..31 of the stored word are `(address >> 4)` masked to 24 bits. For a shader in `0x0CBE9000..0x0CBE9FFF` (bytes 25584..29679 of this art block) that word's high half is `0xCBE9`. Little-endian, that half sits at `symbol_type+14`.

### Why that word is the method count

`lower_sh4_ir.py:1489-1552` uses the IR offset as the byte displacement from the object pointer. The compiler has already subtracted the boxed offset: `get_field_of_structure` (`goalc/compiler/compilation/Type.cpp:705-710`) adds `field.offset() - type->get_offset()`, and `BasicType::get_offset()` is `BASIC_OFFSET` (4). On a `type` object the type-system layout (`TypeSystem.cpp:1202-1208`, `kscheme.h:30-37`, `goal_layout.h:77-82`) is:

| Field | Type-system offset | IR / object offset |
| --- | --- | --- |
| `symbol` | 4 | 0 |
| `parent` | 8 | 4 |
| `size` | 12 | 8 (`uint16`) |
| `psize` | 14 | 10 (`uint16`) |
| `heap-base` | 16 | 12 (`uint16`) |
| `allocated-length` (`num_methods`) | 18 | 14 (`uint16`) |
| method 0 | 20 | 16 |

The 4-byte bitfield store at object offset 12 covers `heap-base` and `allocated-length`. `dc_goal_init` (`goal_runtime.c:491`) sets `num_methods` to 9 and leaves `heap_base` at 0. After the store the half at +14 is 52201, so `type_of` fails for every symbol.

The same even page with another index writes a sibling word of this same type object: index 0 at `+0` (`symbol`), 1 at `+4` (`parent`), 2 at `+8` (`size`/`psize`), 4 at `+16` (method 0). The probe sampled only `+14`. Those other words may be smashed too.

`set-loaded-art` (`loader.gc:80-83`) and `(set! *merc-ctrl-header* ...)` (`merc.gc:168`) store a raw pointer. A raw eichar pointer leaves `0x0CBE` in a high half, and `set_symbol` writes the symbol value cell (`lower_sh4_ir.py:1486-1488`), which is `≡ 4` (mod 8), not `symbol_type+12`. The art directory those stores use was a real bounded `load-dir-art-group` before this window (`arena_jak.c:99-107`). The halfword `0xCBE9` selects the shader-ptr insert.

### Fix

Load the retail `dir-tpages` file and let original `texture-page-dir.relocate` (`texture.gc:1721-1725`) set `*texture-page-dir*` before `dc_arena_jak_art_load`. That is the same kind of missing boot init as `*cam-layout*`: `texture-h.gc:212` defines the global as `#f`, and the assignment lives in a file this headless boot never loads. With the real directory, `link` is `#f` until a texture page allocates its link array, and `texture.gc:2034` skips the store. Login itself stays as it is. Patching `texture.gc:2037`, or writing 9 back into `num_methods`, leaves the next even-page shader free to smash the same word, and leaves any sibling words already written by other indices.
