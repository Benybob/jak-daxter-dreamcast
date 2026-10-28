/* Host oracle for the mips2c sky case (AI-assisted), ISC.
 *
 * Runs game/mips2c/jak1_functions/sky_tng.cpp unchanged on the host, as desktop
 * does: GOAL addresses are offsets into one buffer (g_ee_main_mem), each call gets
 * the trampoline's ExecutionContext, and real SSE implements vu.h. The case is
 * mips2c_sky_case.h; the Flycast boot runs the same bytes through the native
 * SH-4 build and compares the result to what this prints. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "game/kernel/jak1/kscheme.h"
#include "game/mips2c/mips2c_private.h"

extern "C" {
#include "mips2c_sky_case.h"
}

u8* g_ee_main_mem = nullptr;
u8* g_ee_main_mem_exec = nullptr;
Mips2C::LinkedFunctionTable Mips2C::gLinkedFunctionTable;

namespace {
constexpr u32 kMemory = 0x40000, kSymbols = 0x100, kRegion = 0x10000, kStackTop = 0x3f000;
alignas(16) u8 memory[kMemory];
std::map<std::string, u32> symbols;
struct Entry {
  u64 (*exec)(void*);
  u32 stack;
};
std::map<std::string, Entry> table;

u32 cell(const char* name) {
  auto it = symbols.find(name);
  if (it != symbols.end()) return it->second;
  u32 at = kSymbols + 8 * (u32)symbols.size();
  symbols[name] = at;
  return at;
}

u64 call(const char* name, u64 a0 = 0, u64 a1 = 0) {
  using namespace Mips2C;
  auto it = table.find(name);
  if (it == table.end()) {
    fprintf(stderr, "%s not registered\n", name);
    exit(2);
  }
  ExecutionContext c;
  memset(&c, 0, sizeof c);
  c.gprs[Mips2C::a0].du64[0] = a0;
  c.gprs[Mips2C::a1].du64[0] = a1;
  c.gprs[Mips2C::sp].du64[0] = kStackTop;
  return it->second.exec(&c);
}
}  // namespace

Ptr<jak1::Symbol> jak1::intern_from_c(const char* name) {
  return Ptr<jak1::Symbol>(cell(name));
}
void Mips2C::LinkedFunctionTable::reg(const std::string& name, u64 (*exec)(void*), u32 stack) {
  table[name] = {exec, stack};
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
extern "C" u64 _call_goal8_asm_win32(void*, u64*, u64, u64, u64, void*) {
  fprintf(stderr, "jalr into GOAL is not part of the sky case\n");
  exit(4);
}
extern "C" u64 _call_goal8_asm_systemv(void*, u64*, u64, u64, u64, void*) {
  fprintf(stderr, "jalr into GOAL is not part of the sky case\n");
  exit(4);
}

namespace Mips2C::jak1 {
namespace init_sky_regs { void link(); }
namespace set_sky_vf27 { void link(); }
namespace set_sky_vf23_value { void link(); }
namespace render_sky_tri { void link(); }
namespace draw_large_polygon { void link(); }
namespace clip_polygon_against_positive_hyperplane { void link(); }
namespace clip_polygon_against_negative_hyperplane { void link(); }
}  // namespace Mips2C::jak1

int main() {
  using namespace Mips2C::jak1;
  g_ee_main_mem = memory;
  init_sky_regs::link();
  set_sky_vf27::link();
  set_sky_vf23_value::link();
  render_sky_tri::link();
  draw_large_polygon::link();
  clip_polygon_against_positive_hyperplane::link();
  clip_polygon_against_negative_hyperplane::link();
  u8* region = memory + kRegion;
  dc_sky_fill(region, kRegion);
  auto set = [](const char* name, u32 value) { memcpy(memory + cell(name), &value, 4); };
  set("*math-camera*", kRegion + DC_SKY_CAMERA);
  set("*sky-tng-data*", kRegion + DC_SKY_DATA);
  set("*fake-scratchpad-data*", kRegion + DC_SKY_SPAD);
  call("init-sky-regs");
  call("set-sky-vf27", kRegion + DC_SKY_VF27);
  call("set-sky-vf23-value", DC_SKY_VF23);
  call("render-sky-tri", kRegion + DC_SKY_VERTS, kRegion + DC_SKY_DMA);
  u32 bytes = dc_sky_normalize(region, kRegion);
  printf("#define DC_SKY_EXPECT_BYTES %uu\n#define DC_SKY_EXPECT_CRC 0x%08xu\n", bytes,
         dc_sky_crc32(region, DC_SKY_REGION));
  return 0;
}
