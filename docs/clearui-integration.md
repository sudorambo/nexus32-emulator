# ClearUI Debug Overlay — Integration Guide

How ClearUI 1.1.1 is integrated into `nexus32-emulator` as a debug overlay,
composited over the Vulkan framebuffer.

---

## Architecture Overview

```
┌─────────────┐     RGBA framebuffer      ┌──────────────┐
│  debug_ui.c │  ──────────────────────►  │ vulkan_init.c│
│  (ClearUI)  │  cui_rdi_soft renders     │  (Vulkan)    │
│             │  to a CPU-side buffer     │              │
└─────────────┘                           └──────┬───────┘
                                                 │
                                          staging buf → image
                                          draw fullscreen tri
                                          alpha-blend over scene
```

ClearUI runs entirely in software (`cui_rdi_soft`). Each frame, `debug_ui.c`
drives a ClearUI frame, producing an RGBA pixel buffer. `vulkan_init.c` uploads
that buffer to a GPU texture via a staging buffer and draws it as a
fullscreen-triangle overlay with alpha blending.

---

## Build Setup (CMakeLists.txt)

### 1. Point to ClearUI

```cmake
set(CLEARUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../clearui-1.1.1" CACHE PATH "Path to ClearUI")
```

Override at configure time with `-DCLEARUI_DIR=/path/to/clearui` if needed.

### 2. Detect and collect sources

```cmake
set(CLEARUI_FOUND FALSE)
if(NOT NXEMU_HEADLESS AND EXISTS "${CLEARUI_DIR}/include/clearui.h")
  set(CLEARUI_FOUND TRUE)
  set(CLEARUI_SOURCES
    ${CLEARUI_DIR}/src/core/arena.c
    ${CLEARUI_DIR}/src/core/frame_alloc.c
    ${CLEARUI_DIR}/src/core/vault.c
    ${CLEARUI_DIR}/src/core/utf8.c
    ${CLEARUI_DIR}/src/core/version.c
    ${CLEARUI_DIR}/src/core/context.c
    ${CLEARUI_DIR}/src/core/draw_cmd.c
    ${CLEARUI_DIR}/src/core/node.c
    ${CLEARUI_DIR}/src/core/diff.c
    ${CLEARUI_DIR}/src/core/render.c
    ${CLEARUI_DIR}/src/core/a11y.c
    ${CLEARUI_DIR}/src/font/atlas.c
    ${CLEARUI_DIR}/src/layout/layout.c
    ${CLEARUI_DIR}/src/widget/layout.c
    ${CLEARUI_DIR}/src/widget/label.c
    ${CLEARUI_DIR}/src/widget/button.c
    ${CLEARUI_DIR}/src/widget/checkbox.c
    ${CLEARUI_DIR}/src/widget/spacer.c
    ${CLEARUI_DIR}/src/widget/icon_button.c
    ${CLEARUI_DIR}/src/widget/scroll.c
    ${CLEARUI_DIR}/src/widget/text_input.c
    ${CLEARUI_DIR}/src/widget/canvas.c
    ${CLEARUI_DIR}/src/rdi/clearui_rdi_soft.c
    ${CLEARUI_DIR}/src/platform/cui_platform_stub.c
  )
endif()
```

### 3. Compile definitions and includes

```cmake
if(CLEARUI_FOUND)
  target_compile_definitions(nxemu PRIVATE NXEMU_CLEARUI CLEARUI_EMBED_FONT)
  target_include_directories(nxemu PRIVATE
    ${CLEARUI_DIR}/include
    ${CLEARUI_DIR}/src
    ${CLEARUI_DIR}/src/core
    ${CLEARUI_DIR}/src/layout
    ${CLEARUI_DIR}/src/font
    ${CLEARUI_DIR}/src/widget
    ${CLEARUI_DIR}/src/platform
    ${CLEARUI_DIR}/src/rdi
  )
  target_compile_options(nxemu PRIVATE -Wno-unused-parameter)
  target_link_libraries(nxemu m)
endif()
```

Key defines:
- **`NXEMU_CLEARUI`** — enables the ClearUI code path in `debug_ui.c`.
- **`CLEARUI_EMBED_FONT`** — tells ClearUI's `atlas.c` to use the compiled-in
  default font instead of loading `deps/default_font.ttf` at runtime.

---

## CPU Side: debug_ui.c

All ClearUI interaction is in `src/debug/debug_ui.c`, guarded by
`#ifdef NXEMU_CLEARUI`.

### Initialization (lazy, on first visible frame)

```c
#include <clearui.h>
#include <clearui_rdi.h>
#include <clearui_platform.h>

static cui_ctx *s_ctx;
static cui_rdi_context *s_rdi_ctx;

static void ensure_initialized(void)
{
    if (s_initialized) return;
    cui_rdi_soft_get()->init(&s_rdi_ctx);           // software rasterizer
    cui_config config = { "Debug", 480, 360, 1.0f, 0, NULL, NULL };
    s_ctx = cui_create(&config);
    cui_set_rdi(s_ctx, cui_rdi_soft_get(), s_rdi_ctx);
    cui_set_platform(s_ctx, cui_platform_stub_get(), NULL);  // no real input

    cui_theme dark;
    cui_theme_dark(&dark);        // light text on dark — readable over dimmed scene
    cui_set_theme(s_ctx, &dark);

    s_initialized = 1;
}
```

### Per-frame rendering

```c
int debug_ui_frame(cpu_state_t *cpu, nexus32_mem_t *mem)
{
    if (!debug_overlay_visible) return 0;
    ensure_initialized();
    cui_rdi_soft_set_viewport(s_rdi_ctx, OVERLAY_W, OVERLAY_H);
    cui_begin_frame(s_ctx);
    build_debug_ui(s_ctx, cpu, mem);   // your widgets go here
    cui_end_frame(s_ctx);
    return 1;
}
```

### Retrieving the framebuffer

```c
void debug_ui_get_framebuffer(const void **out_rgba, int *out_w, int *out_h)
{
    cui_rdi_soft_get_framebuffer(s_rdi_ctx, out_rgba, out_w, out_h);
}
```

Returns a pointer to the RGBA pixel buffer that the soft RDI rendered into.
No copy — this points directly into ClearUI's internal buffer.

---

## GPU Side: vulkan_init.c

### Shaders

ClearUI 1.1.1 ships pre-compiled SPIR-V embedded in a header:

```c
#include <clearui_overlay_spv.h>
// provides: clearui_overlay_vert_spv[], clearui_overlay_frag_spv[]
```

- **Vertex shader**: generates a fullscreen triangle from `gl_VertexIndex` — no
  vertex buffer needed.
- **Fragment shader**: samples the overlay texture, applies alpha blending,
  supports a `swizzle_rb` push constant for RGBA↔BGRA format handling.

### Pipeline setup

1. Create shader modules from `clearui_overlay_vert_spv` / `clearui_overlay_frag_spv`.
2. Overlay image: `VK_FORMAT_R8G8B8A8_UNORM`, 512×512, sampled + transfer-dst.
3. Staging buffer: host-visible, same size, for CPU→GPU upload.
4. Descriptor set: single `COMBINED_IMAGE_SAMPLER`.
5. Pipeline layout: one descriptor set + one push constant (`int32_t swizzle_rb`).
6. Pipeline: no vertex input, triangle list, alpha blending enabled, compatible
   with the main render pass.

### Drawing the overlay (critical Vulkan sequencing)

The overlay draw in `vulkan_draw_overlay()` follows this sequence:

```
1. memcpy RGBA framebuffer → staging buffer  (no CPU swizzle needed)
2. vkCmdEndRenderPass       ← end the clear pass
3. barrier: image → TRANSFER_DST_OPTIMAL
4. vkCmdCopyBufferToImage   ← upload texture (must be outside render pass)
5. barrier: image → SHADER_READ_ONLY_OPTIMAL
6. vkCmdBeginRenderPass     ← new pass with LOAD_OP_LOAD (preserves scene)
7. bind pipeline, push swizzle constant, draw 3 vertices
8. vkCmdEndRenderPass
```

**Important**: `vkCmdCopyBufferToImage` is a transfer operation and **must not**
be called inside an active render pass. The emulator uses two compatible render
passes:

- `s_render_pass` — `LOAD_OP_CLEAR`, used for the initial scene clear.
- `s_render_pass_load` — `LOAD_OP_LOAD`, used when restarting the pass after
  the texture upload so the cleared background is preserved.

Both passes are compatible (same attachment format, sample count, final layout)
so the overlay pipeline works with either.

---

## Gotchas & Lessons Learned

1. **Embedded font**: Always define `CLEARUI_EMBED_FONT`. Without it, ClearUI
   tries to load `deps/default_font.ttf` relative to the working directory,
   which silently fails and produces invisible text.

2. **Transfer ops outside render passes**: Vulkan requires
   `vkCmdCopyBufferToImage` to be recorded outside a render pass. Some drivers
   silently ignore this violation; others (notably newer AMD RDNA) drop the
   command entirely, giving you a transparent texture.

3. **LOAD_OP_LOAD for multi-pass**: When splitting a frame across two render
   passes, the second pass must use `VK_ATTACHMENT_LOAD_OP_LOAD` with
   `initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` to keep what the
   first pass rendered.

4. **Dark theme**: ClearUI defaults to black text. For overlays on dark
   backgrounds, call `cui_theme_dark()` + `cui_set_theme()` during init.

5. **No vertex buffer**: ClearUI's overlay shaders generate geometry from
   `gl_VertexIndex`, so the pipeline needs empty vertex input state and
   `vkCmdDraw(cmd, 3, 1, 0, 0)`.

6. **Push constant for swizzle**: The fragment shader has a `swizzle_rb` push
   constant (`int32_t`, fragment stage, offset 0). Set to 1 if your swapchain
   uses BGRA, 0 for RGBA. The emulator auto-detects this from `s_swap_fmt`.

---

## Adding / Changing Widgets

Edit `build_debug_ui()` in `debug_ui.c`. ClearUI is immediate-mode:

```c
cui_label(ctx, "Hello");
cui_layout row = { .gap = 8 };
cui_row(ctx, &row);
  cui_label(ctx, "Left");
  cui_label(ctx, "Right");
cui_end(ctx);
```

Use `cui_frame_printf(ctx, fmt, ...)` for formatted strings — it allocates from
ClearUI's per-frame arena, so no manual buffer management is needed.

Available widgets: `cui_label`, `cui_button`, `cui_checkbox`, `cui_text_input`,
`cui_spacer`, `cui_icon_button`, `cui_scroll`, `cui_canvas`.

Layout containers: `cui_row`, `cui_column`, each closed with `cui_end`.

---

## Overlay Dimensions

Currently hardcoded:

```c
#define OVERLAY_W 480
#define OVERLAY_H 360
```

The GPU-side texture is allocated at `OVERLAY_MAX_SIZE` (512×512) in
`vulkan_init.c`. If you increase the overlay size, update `OVERLAY_MAX_SIZE` to
match (must be >= max of width, height).
