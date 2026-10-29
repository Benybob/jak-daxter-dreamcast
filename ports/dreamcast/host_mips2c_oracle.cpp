/* Host oracle for the mips2c differential cases (AI-assisted), ISC.
 *
 * Runs the unchanged game/mips2c files on the host the way desktop does: GOAL
 * addresses are offsets into one buffer (g_ee_main_mem), biased to a native P0
 * magnitude (0x0c300000 + offset) so packed data can never look like a region
 * pointer to the normalization in mips2c_cases.h. Each call gets the
 * desktop trampoline's ExecutionContext, and real SSE implements vu.h. Calls go
 * through the same generated wrappers as the SH-4 build (mips2c_dc_table.inc).
 * Prints, for each case in mips2c_cases.h, the normalized region's CRC-32 and the
 * case's result; the Flycast boot compares against them. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "game/kernel/jak1/kscheme.h"
#include "game/mips2c/mips2c_private.h"

extern "C" {
#include "mips2c_cases.h"
}

u8* g_ee_main_mem = nullptr;  /* memory - kBias */
u8* g_ee_main_mem_exec = nullptr;
Mips2C::LinkedFunctionTable Mips2C::gLinkedFunctionTable;

namespace {
constexpr u32 kBias = 0x0c300000, kMemory = 0x40000;
constexpr u32 kSymbols = kBias + 0x100, kFalse = kBias + 0x0f00, kRegion = kBias + 0x10000,
              kStackTop = kBias + 0x3f000;
/* Function addresses jalr can reach: a case's GOAL stand-ins, then the built mips2c
 * functions (desktop's trampoline for them). Nothing is stored there. */
constexpr u32 kGoalSlots = kBias + 0x1000, kMips2cEntries = kBias + 0x1100;
alignas(16) u8 memory[kMemory];
std::map<std::string, u32> symbols;
dc_m2c_env* g_env = nullptr;
dc_m2c_goal_fn g_goal[DC_M2C_GOAL_SLOTS];
u32 g_stack_top = kStackTop;  /* nested calls (jalr into mips2c) get the space below */

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

u8* host_ptr(u32 address) {
  return memory + (address - kBias);
}

u32 cell(const char* name) {
  auto it = symbols.find(name);
  if (it != symbols.end()) return it->second;
  u32 at = kSymbols + 8 * (u32)symbols.size();
  if (at + 8 > kFalse) {
    fprintf(stderr, "symbol area full\n");
    exit(2);
  }
  symbols[name] = at;
  return at;
}

/* Desktop's trampoline: arguments a0-t3, the symbol table in s7, a stack in sp. */
u64 dc_run(unsigned index, const u64* args) {
  using Mips2C::ExecutionContext;
  if (!g_slots[index].exec) {
    fprintf(stderr, "%s not registered\n", dc_entries[index].name);
    exit(2);
  }
  ExecutionContext c;
  memset(&c, 0, sizeof c);
  const int positions[8] = {Mips2C::a0, Mips2C::a1, Mips2C::a2, Mips2C::a3,
                            Mips2C::t0, Mips2C::t1, Mips2C::t2, Mips2C::t3};
  for (int i = 0; i < 8; ++i) c.gprs[positions[i]].du64[0] = args[i];
  c.gprs[Mips2C::s7].du64[0] = kFalse;
  /* As the native build: at enters holding the scratchpad (the PS2 callers' convention). */
  memcpy(&c.gprs[Mips2C::at].du32[0], host_ptr(cell("*fake-scratchpad-data*")), 4);
  const u32 top = g_stack_top;
  g_stack_top = (top - g_slots[index].stack - 16) & ~15u;
  c.gprs[Mips2C::sp].du64[0] = top;
  const u64 result = g_slots[index].exec(&c);
  g_stack_top = top;
  return result;
}

void set_symbol(dc_m2c_env*, const char* name, uint32_t value) {
  memcpy(host_ptr(cell(name)), &value, 4);
}
uint64_t call(dc_m2c_env*, const char* name, const uint64_t* args) {
  for (unsigned i = 0; i < kEntries; ++i)
    if (!strcmp(dc_entries[i].name, name)) return dc_entries[i].generic(args);
  fprintf(stderr, "%s is not built\n", name);
  exit(2);
}
uint32_t goal_function(dc_m2c_env*, unsigned slot, dc_m2c_goal_fn fn) {
  if (slot >= DC_M2C_GOAL_SLOTS) return 0;
  g_goal[slot] = fn;
  return kGoalSlots + 16 * slot;
}
uint32_t mips2c_function(dc_m2c_env*, const char* name) {
  for (unsigned i = 0; i < kEntries; ++i)
    if (!strcmp(dc_entries[i].name, name)) return kMips2cEntries + 16 * i;
  fprintf(stderr, "%s is not built\n", name);
  exit(2);
}
uint32_t symbol_at(dc_m2c_env*, const char* name) {
  return cell(name);
}
void* memory_at(dc_m2c_env*, uint32_t address) {
  return host_ptr(address);
}
/* jalr: desktop's trampoline into the function at addr. */
u64 call_goal(void* fn, const u64* args) {
  const u32 addr = (u32)((u8*)fn - g_ee_main_mem);
  if (addr - kGoalSlots < 16 * DC_M2C_GOAL_SLOTS && g_goal[(addr - kGoalSlots) / 16])
    return g_goal[(addr - kGoalSlots) / 16](g_env, args);
  if (addr - kMips2cEntries < 16 * kEntries) return dc_entries[(addr - kMips2cEntries) / 16].generic(args);
  fprintf(stderr, "a case reached jalr into GOAL at %#x with no stand-in\n", addr);
  exit(4);
}
}  // namespace

Ptr<jak1::Symbol> jak1::intern_from_c(const char* name) {
  return Ptr<jak1::Symbol>(cell(name));
}
void Mips2C::LinkedFunctionTable::reg(const std::string& name, u64 (*exec)(void*), u32 stack) {
  for (unsigned i = 0; i < kEntries; ++i)
    if (name == dc_entries[i].name) g_slots[i] = {exec, stack};
}
u32 Mips2C::LinkedFunctionTable::get(const std::string&) {
  return 0;
}
[[noreturn]] void private_assert_failed(const char* expr, const char* file, int line, const char*, const char*) {
  fprintf(stderr, "assert %s at %s:%d\n", expr, file, line);
  exit(3);
}
[[noreturn]] void private_assert_failed(const char* expr, const char* file, int line, const char*,
                                        const std::string_view&) {
  fprintf(stderr, "assert %s at %s:%d\n", expr, file, line);
  exit(3);
}
/* jalr into GOAL: there is no GOAL here, only a case's stand-ins and the built
 * mips2c functions. */
extern "C" u64 _call_goal8_asm_win32(void* fn, u64* args, u64, u64, u64, void*) {
  return call_goal(fn, args);
}
extern "C" u64 _call_goal8_asm_systemv(void* fn, u64* args, u64, u64, u64, void*) {
  return call_goal(fn, args);
}

int main() {
  g_ee_main_mem = memory - kBias;
  for (auto link : dc_links) link();
  dc_m2c_env env;
  env.region = host_ptr(kRegion);
  env.base = kRegion;
  env.s7 = kFalse;
  env.set_symbol = set_symbol;
  env.call = call;
  env.call_jalr = call;  /* desktop's jalr reaches the same exec through its trampoline */
  env.goal_function = goal_function;
  env.mips2c_function = mips2c_function;
  env.memory = memory_at;
  env.symbol = symbol_at;
  g_env = &env;
  env.context = nullptr;
  printf("#define DC_M2C_EXPECT_COUNT %uu\n", (unsigned)DC_M2C_CASE_COUNT);
  printf("static const struct { const char *name; uint32_t crc, result; } dc_m2c_expect[] = {\n");
  for (unsigned i = 0; i < DC_M2C_CASE_COUNT; ++i) {
    fprintf(stderr, "case %s\n", dc_m2c_cases[i].name);
    uint32_t result, crc = dc_m2c_run_case(&env, i, &result);
    memset(g_goal, 0, sizeof g_goal);
    printf("  {\"%s\", 0x%08xu, %uu},\n", dc_m2c_cases[i].name, crc, result);
  }
  printf("};\n");
  return 0;
}
