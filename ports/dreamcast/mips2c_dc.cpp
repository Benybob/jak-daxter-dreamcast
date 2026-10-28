/* Native SH-4 services for the PC port's mips2c functions (AI-assisted), ISC.
 *
 * game/mips2c/jak1_functions/*.cpp build for SH-4 unchanged. On desktop, GOAL
 * calls a function through a trampoline (asm_funcs_x86_64.asm _mips2c_call_*)
 * that builds an ExecutionContext: arguments a0-t3, pp in s6, the symbol table
 * in s7, and a stack for the translated code in sp. Here, build_mips2c.py
 * generates one C wrapper per registered function with the GOAL function's own
 * native signature (floats in FPU registers, as the lowered GOAL code passes
 * them) and the wrapper builds the same context. Desktop GOAL passes every
 * argument in a GPR, so a float argument is stored as its bits, and a float
 * result is returned from v0's bits. */
#include <alloca.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "game/kernel/jak1/kscheme.h"
#include "game/mips2c/mips2c_private.h"

extern "C" {
#include "goal_runtime.h"
#include "mips2c_dc.h"
uint32_t dc_goal_mips2c_current_process(void);  /* goal_bridge.c */
}

/* GOAL addresses are C addresses (cached P0, MMU off): no base offset. Defined
 * here rather than as a constant so no translation unit can assume null. */
u8* g_ee_main_mem = nullptr;
u8* g_ee_main_mem_exec = nullptr;

namespace {
dc_goal_runtime* g_rt = nullptr;
uint32_t g_spad_symbol = 0;  /* *fake-scratchpad-data* */
uint32_t g_calls = 0;
uint32_t g_jalr = 0;
char g_assert[160];

struct Slot {
  u64 (*exec)(void*);
  u32 stack;
};

u64 to_bits(float f) {
  u32 v;
  memcpy(&v, &f, 4);
  return v;
}
float from_bits(u64 v) {
  u32 low = (u32)v;
  float f;
  memcpy(&f, &low, 4);
  return f;
}
u64 dc_run(unsigned index, const u64* args);
}  // namespace

#include "build/mips2c/mips2c_dc_table.inc"

namespace {
constexpr unsigned kEntries = sizeof(dc_entries) / sizeof(dc_entries[0]);
Slot g_slots[kEntries];

/* One call: the desktop trampoline's ExecutionContext and translated-code stack. */
u64 dc_run(unsigned index, const u64* args) {
  using Mips2C::ExecutionContext;
  const Slot& slot = g_slots[index];
  if (!slot.exec || !g_rt) {
    if (g_rt) g_rt->error = DC_GOAL_UNSUPPORTED_TRANSFER;
    return 0;
  }
  ++g_calls;
  ExecutionContext c;
  memset(&c, 0, sizeof c);
  const int positions[8] = {Mips2C::a0, Mips2C::a1, Mips2C::a2, Mips2C::a3,
                            Mips2C::t0, Mips2C::t1, Mips2C::t2, Mips2C::t3};
  for (int i = 0; i < 8; ++i) c.gprs[positions[i]].du64[0] = args[i];
  c.gprs[Mips2C::s6].du64[0] = dc_goal_mips2c_current_process();
  c.gprs[Mips2C::s7].du64[0] = g_rt->s7;
  /* The PS2 callers of the generic/merc functions enter with at holding the
   * scratchpad, and those functions save registers at 12432(at) and up (the
   * generic work's save area) before setting it. Desktop leaves at uninitialized. */
  if (g_spad_symbol) memcpy(&c.gprs[Mips2C::at].du32[0], (const void*)(uintptr_t)g_spad_symbol, 4);
  u8* stack = (u8*)alloca(slot.stack + 16);
  c.gprs[Mips2C::sp].du64[0] = ((uintptr_t)stack + slot.stack + 15) & ~(uintptr_t)15;
  return slot.exec(&c);
}
}  // namespace

/* kscheme intern_from_c: the symbol's native (P0) address, as an offset from 0. */
Ptr<jak1::Symbol> jak1::intern_from_c(const char* name) {
  return Ptr<jak1::Symbol>(g_rt ? dc_goal_intern(g_rt, name) : 0);
}

/* Registrations from each file's link(): matched to the generated wrapper by name. */
void Mips2C::LinkedFunctionTable::reg(const std::string& name, u64 (*exec)(void*), u32 stack_size) {
  for (unsigned i = 0; i < kEntries; ++i) {
    if (name == dc_entries[i].name) {
      g_slots[i].exec = exec;
      g_slots[i].stack = stack_size;
      return;
    }
  }
}
u32 Mips2C::LinkedFunctionTable::get(const std::string& name) {
  return dc_mips2c_find(name.c_str());
}
Mips2C::LinkedFunctionTable Mips2C::gLinkedFunctionTable;

/* Assert.cpp: a failed mips2c assertion is recorded and faults the runtime; the
 * translated code is not resumed. */
[[noreturn]] static void assert_failed(const char* expr, const char* file, int line) {
  if (!g_assert[0]) snprintf(g_assert, sizeof g_assert, "%s:%d: %s", file, line, expr);
  if (g_rt) g_rt->error = DC_GOAL_BAD_ARGUMENT;
  printf("mips2c assertion failed: %s\n", g_assert);
  for (;;) {
  }
}
[[noreturn]] void private_assert_failed(const char* expr,
                                        const char* file,
                                        int line,
                                        const char*,
                                        const char*) {
  assert_failed(expr, file, line);
}
[[noreturn]] void private_assert_failed(const char* expr,
                                        const char* file,
                                        int line,
                                        const char*,
                                        const std::string_view&) {
  assert_failed(expr, file, line);
}

extern "C" unsigned dc_mips2c_install(dc_goal_runtime* rt) {
  unsigned i, linked = 0;
  if (!rt) return 0;
  g_rt = rt;
  g_spad_symbol = dc_goal_intern(rt, "*fake-scratchpad-data*");
  for (i = 0; i < sizeof dc_links / sizeof dc_links[0]; ++i) dc_links[i]();
  for (i = 0; i < kEntries; ++i) linked += g_slots[i].exec != nullptr;
  return linked == kEntries ? linked : 0;
}
extern "C" uint32_t dc_mips2c_find(const char* name) {
  for (unsigned i = 0; i < kEntries; ++i)
    if (!strcmp(name, dc_entries[i].name) && g_slots[i].exec) return dc_entries[i].entry;
  return 0;
}
/* goal_bridge.c's dc-mips2c:NAME stub: mov.l (the entry at +8),r0; jmp @r0; nop; nop; then the
 * C entry. */
extern "C" bool dc_mips2c_jalr(u32 addr, const u64* args, u64* result) {
  static const unsigned char stub[8] = {1, 0xd0, 0x2b, 0x40, 9, 0, 9, 0};
  uint32_t target;
  if (!addr || (addr & 1) || memcmp((const void*)(uintptr_t)addr, stub, 8)) return false;
  memcpy(&target, (const void*)(uintptr_t)(addr + 8), 4);
  for (unsigned i = 0; i < kEntries; ++i) {
    if (dc_entries[i].entry == target) {
      ++g_jalr;
      *result = dc_entries[i].generic(args);
      return true;
    }
  }
  return false;
}
/* For the differential cases: calls NAME the way translated code calls another
 * mips2c function, with jalr on the GOAL stub GAME.CGO's lookup made. */
extern "C" uint64_t dc_mips2c_call_jalr(const char* name, const uint64_t* args) {
  char label[96];
  uint32_t symbol, stub;
  if (!g_rt) return 0;
  snprintf(label, sizeof label, "dc-mips2c:%s", name);
  symbol = dc_goal_find(g_rt, label);
  stub = symbol ? dc_goal_symbol_value(g_rt, symbol) : 0;
  if (!stub) {
    g_rt->error = DC_GOAL_BAD_FUNCTION;
    return 0;
  }
  Mips2C::ExecutionContext c;
  memset(&c, 0, sizeof c);
  const int positions[8] = {Mips2C::a0, Mips2C::a1, Mips2C::a2, Mips2C::a3,
                            Mips2C::t0, Mips2C::t1, Mips2C::t2, Mips2C::t3};
  for (int i = 0; i < 8; ++i) c.gprs[positions[i]].du64[0] = args[i];
  c.jalr(stub);
  return c.gprs[Mips2C::v0].du64[0];
}
extern "C" uint32_t dc_mips2c_jalr_calls(void) {
  return g_jalr;
}
extern "C" uint64_t dc_mips2c_call(const char* name, const uint64_t* args) {
  for (unsigned i = 0; i < kEntries; ++i)
    if (!strcmp(name, dc_entries[i].name) && g_slots[i].exec) return dc_entries[i].generic(args);
  if (g_rt) g_rt->error = DC_GOAL_BAD_FUNCTION;
  return 0;
}
extern "C" uint32_t dc_mips2c_calls(void) {
  return g_calls;
}
extern "C" const char* dc_mips2c_assert_text(void) {
  return g_assert[0] ? g_assert : nullptr;
}
