# Contributing to the Dreamcast workbench

2026-10-21. (AI-assisted). This independent fork needs compiler/ABI, GOAL,
KallistiOS, PowerVR and hardware-testing help. It is research toward a playable
adaptation, not a playable release. Please start with one bounded task from
[HELP_WANTED.md](ports/dreamcast/HELP_WANTED.md).

1. Read [BUILDING.md](ports/dreamcast/BUILDING.md), reproduce the relevant check,
   and explain the proposed approach before undertaking a large subsystem.
2. Keep changes focused. Describe the failing input, resulting behavior, exact
   toolchain/options, tests run and any unsupported cases that remain.
3. For compiler changes, retain fail-closed behavior for unsupported IR/types.
   Validate byte encodings against GNU assembler, ABI details against SH-4 GCC,
   and behavior in Flycast with stock 16 MiB RAM when applicable.
4. Report code/data/stack sizes and transient allocations separately. Never use
   emulator fps to claim real-console performance or combine separate programs'
   memory counts into a whole-game budget.
5. Include source/synthetic fixtures only. Do not upload ISOs, BIOS files, game
   archives, extracted or converted assets, saves, keys or credentials. An ELF
   can embed game data; do not assume binaries are safe to distribute.
6. Disclose AI tools and their extent, review the resulting code yourself, and
   explain the implementation. See [AI_POLICY.md](AI_POLICY.md).

Human contributors may submit focused pull requests to **Benybob/jak-daxter-dreamcast**.
Please direct Dreamcast support and recruitment to this fork. OpenGOAL maintainers
have not agreed to support this adaptation; respect their separate policies and
do not send this experimental AI-assisted work to upstream as an unreviewed dump.
No fundraising, paid work or delivery dates are implied by this invitation.
