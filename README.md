# nexus32-emulator

Vulkan-based emulator for the NEXUS-32 fantasy game console. Loads `.nxrom` ROMs, implements the virtual machine (CPU, memory, GPU command buffer, DMA, APU, input, timers), and translates rendering to Vulkan per the [NEXUS-32 specification](../nexus32-spec/NEXUS32_Specification_v1.0.md) §12.

## Build

- **Dependencies**: C17 compiler (GCC/Clang). For windowed mode: Vulkan SDK 1.3+, SDL2 2.28+ (see spec §13.2). The debug overlay requires [ClearUI 1.1.1](../clearui-1.1.1/) (auto-detected by CMake).
- **Headless**: If Vulkan or SDL2 is not found, CMake builds with `NXEMU_HEADLESS`; the emulator runs one frame and exits (no window).
- **Compiler warnings**: The build enables `-Wall -Wextra -Wpedantic` by default.

```bash
mkdir build && cd build
cmake ..
make
```

Executable: `nxemu` (or `build/nxemu`).

## Usage

```bash
nxemu <rom.nxrom>
```

- **Windowed** (when built with Vulkan+SDL2): Opens a window, runs the main loop (input → CPU → GPU PRESENT → VBlank), 60 FPS. Escape to quit.
- **Headless**: Loads the ROM, runs one frame of CPU (cycle_budget), then exits.

**Save data**: EEPROM is persisted to `<rom_basename>.sav` on each PRESENT and on exit. The file is loaded at startup if present.

## Debug Overlay

- **F12** (windowed): Toggle the ClearUI debug overlay — displays all 32 registers, disassembly at PC, and memory dump, composited over the Vulkan framebuffer with a dark theme.
- See [docs/clearui-integration.md](docs/clearui-integration.md) for build setup, Vulkan integration details, and how to add widgets.

## CPU Conformance

The emulator implements the full integer ISA per spec section 2.3:

- **Arithmetic**: ADD/ADDU/SUB/SUBU (ADD/SUB trap on signed overflow), MUL/MULH/DIV/DIVU/MOD with correct cycle costs.
- **Branches**: BEQ/BNE/BLT/BGT/BLE/BGE (2 cycles taken, 1 not-taken).
- **Load/store**: LB/LBU/LH/LHU/LW/SB/SH/SW — unaligned LW/SW/LH/SH/LHU raise address alignment exceptions per spec.
- **Unknown opcodes**: Trigger an illegal instruction exception (not silently treated as NOP).

## Error handling

- Invalid or missing ROM path: clear message and exit code 1.
- Unsupported `format_version`: reports the version and exits.
- Checksum mismatch: reports "header checksum mismatch" or "ROM checksum mismatch".
- Vulkan/window init failure: "failed to init Vulkan/window".

## Conformance

Implementation follows [nexus32-spec/docs/implementation-checklist.md](../nexus32-spec/docs/implementation-checklist.md) for the emulator: ROM loading per [rom-format contract](../nexus32-spec/specs/001-nexus32-spec-baseline/contracts/rom-format.md), SYS_VERSION at 0x0B006000, memory map per §3, DMA per §4, GPU command parsing per §5, and determinism per spec.
