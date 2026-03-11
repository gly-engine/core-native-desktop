# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

core-native-desktop is a minimal native runtime similar in spirit to SDL but structured around explicit **frontends** and **backends**.

Frontends include systems such as browser runtimes, libretro integrations and gly-engine.

Backends implement platform functionality. The most important maintained backend is `lib/Backend_Opengl`.

This document applies to the entire repository but the OpenGL backend is the primary supported rendering path.

Do not inspect or rely on `CMakeLists.txt`.

---

## Build

```bash
# Configure and build (desktop)
cmake -Bbuild -H.
make -C build

# Cross-compile for ARM target
cmake -Bbuild -H. -DTARGET=armv7-gnueabihf-cortex-a7
make -C build
```

The Zig toolchain (`zig-toolchain.cmake`) is used automatically for cross-compilation and downloads Zig 0.15.2 on first use.

**Relevant CMake options:**

| Option | Default | Description |
|---|---|---|
| `GECND_USE_OPENGL` | ON | OpenGL + GLFW backend |
| `GECND_USE_EGL` | OFF | OpenGL ES + EGL backend (embedded) |
| `GECND_USE_LUAJIT` | OFF | LuaJIT instead of Lua 5.4 |
| `GECND_USE_LUA51` | OFF | Lua 5.1 instead of Lua 5.4 |
| `GECND_MOCK_HTTP` | OFF | Mock HTTP with error raising |
| `GECND_USE_PROFILER` | OFF | Google GPerfTools profiling |
| `GECND_AS_LIBRARY` | OFF | Build as static library |
| `GECND_NO_SANITIZE` | OFF | Disable UB sanitizer |

There is no test suite. `BUILD_TESTING` is disabled.

---

## Architecture

### Entry Points

Three main entry point variants live in `src/`:

- `main_basic.c` — simple loop with no libuv
- `main_uv.c` — libuv event loop, timer-driven rendering
- `main_uvsync.c` — libuv event loop with OpenGL frame sync

### Module Layout

```
lib/
  Backend_OpenGL/       primary rendering backend
    pipeline/           batch.c, core.c, shaders.c, video.c
    render/             draw.c, image.c, text.c
    shadder/            GLSL shaders (es_* = GLES, gl_* = desktop)
    window/             egl.c (embedded), glfw.c (desktop)
  Backend_Raylib/       alternative backend
  Backend_SDL2/         alternative backend
  Frontend_Api/         API interface layer
  Frontend_Browser/     browser runtime integration
  Frontend_Core/        core runtime logic
  Frontend_Input/       input dispatch
  Frontend_Libretro/    libretro console emulation frontend
  Frontend_Profile/     performance profiling frontend
  Common_Utils/         shared utilities
  Input_Rc/             remote control input
  Stream_AVlib/         FFmpeg audio/video decoding
  ThirdParty_Api/       JSON, Base64, SQLite, HTTP, etc.
  ThirdParty_Compat/    compatibility shims
```

Public headers are in `include/`. The main API surface is `gecnd.h`.

### Frontend / Backend Contract

Frontends drive application logic and call into the backend through the public API.
Backends are thin platform layers: context creation, resource upload, batch submission, shader execution, frame presentation. No frontend logic belongs in a backend.

### OpenGL Pipeline

The `Backend_OpenGL/pipeline/` layer is the performance-critical core. It accumulates draw commands into large batches sorted by: **shader → depth → texture**. Submissions are deferred until the batch is flushed. This is the primary path for all rendering including FFmpeg video frames and libretro video output.

Shaders are compiled from GLSL sources in `shadder/` and converted to C headers during the build into `build/include/gecnd/`.

---

## Target Hardware

Primary target platform:

ARMv7 Cortex-A7 with Mali-400 GPU.

Characteristics:

* tile based renderer
* limited memory bandwidth
* OpenGL ES class driver
* embedded EGL environments

Desktop development may use GLFW but the embedded EGL path is the main runtime environment.

---

## Rendering Model

Rendering is strictly **2D**.

The pipeline must always support:

* transparency
* depth ordering
* batching

Depth is used only for ordering and batching. It is not used for perspective.

All rendering must be organized into **large batches**.

Batch sorting priority:

1. shader program
2. depth
3. texture

Small draw calls are not acceptable.

---

## GPU Constraints

The Mali-400 is bandwidth constrained.

Design must prioritize:

* minimal driver calls
* minimal state changes
* minimal vertex size
* sequential memory access
* large batch submissions

Framebuffer objects should be avoided.

Rendering should normally target the default framebuffer.

---

## Data Formats

Prefer **16-bit representations**.

Use `int16_t` whenever possible for:

* vertex position
* texture coordinates
* depth

Prefer compact color formats.

Avoid unnecessary use of 32-bit floats.

---

## Video and Texture Sources

The project uses **FFmpeg** for video decoding.

Decoded frames are uploaded directly to OpenGL textures.

libretro frontends also provide video frames that follow the same texture upload path.

The renderer must handle these efficiently without introducing extra copies or intermediate framebuffers.

---

## Event Loop

The runtime uses **libuv** as the event loop.

libuv usage is restricted to files named:

`*_uv.c`

No other files may depend on libuv.

---

## Code Style

Language standard: **C11**

Requirements:

* elegant minimal code
* predictable control flow
* no comments in source files
* clear naming that removes the need for comments

Avoid unnecessary abstractions.

Avoid large framework-style structures.

Prefer simple data-oriented design.

---

## Backend Responsibility

Backends must remain thin platform layers responsible only for:

* context creation
* resource upload
* batch submission
* shader execution
* frame presentation

They must not contain frontend logic or application policy.
