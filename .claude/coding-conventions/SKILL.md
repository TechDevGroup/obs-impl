# OBS Studio Coding Conventions

> **DIRECTIVE**: Consult this skill document FIRST before reading source files. This document is the authoritative reference for coding conventions in this project. Only read source files to verify specific implementation details not covered here.

## Overview

OBS Studio follows Linux kernel normal form (KNF) coding style. This document consolidates all conventions to minimize redundant source exploration.

---

## 1. Formatting Rules

### Indentation & Whitespace
- **Tabs for indentation** (8 columns wide)
- **Spaces for alignment**
- **120 column max line width**
- Unix-style newlines (LF)
- Final newline required at EOF
- No trailing whitespace

### Brace Style
```c
// Functions: opening brace on new line
void function_name(int param)
{
        // 8-space tab indent
        if (condition) {
                // Control statements: brace on same line
        }
}
```

### Pointer Alignment
```c
// Pointer asterisk aligned to variable name (right-aligned)
char *name;
void *data;
```

---

## 2. Naming Conventions

### Functions
- `snake_case` for all functions
- Module prefix followed by underscore: `obs_source_`, `obs_output_`, `obs_encoder_`
- Internal functions: `static` keyword, no export

### Types
- Typedef structs with `_t` suffix: `obs_source_t`, `obs_output_t`
- Opaque types: forward declare in headers, define in .c files

### Macros & Constants
- `UPPER_SNAKE_CASE`
- Prefix with module: `OBS_OUTPUT_VIDEO`, `MAX_CHANNELS`

### Variables
- `snake_case` for all variables
- Descriptive names, avoid single letters except loop counters

---

## 3. File Organization

### Directory Structure
```
libobs/           # Core library
  obs.h           # Public API header
  obs-internal.h  # Internal structures
  obs-*.c         # Implementation files
  obs-*.h         # Component headers
frontend/         # Qt UI application
  components/     # Reusable UI components
  dialogs/        # Dialog windows
  docks/          # Dockable panels
  settings/       # Settings pages
plugins/          # Plugin modules
```

### Header Guards
```c
#pragma once
```

### Include Order
- Preserve existing order (no auto-sorting)
- Local includes before system includes

---

## 4. Core Architecture Patterns

### Reference Counting
```c
// Get reference (increments count)
obs_source_t *source = obs_source_get_ref(weak_source);

// Release when done (decrements count)
obs_source_release(source);

// Weak references for non-owning pointers
obs_weak_source_t *weak = obs_source_get_weak_source(source);
```

### Object Lifecycle
1. `*_create()` - allocate and initialize
2. `*_get_ref()` - obtain reference
3. `*_release()` - release reference
4. `*_destroy()` - called when refcount hits 0

### Signal System
```c
// Define signals
static const char *signals[] = {
        "void event_name(ptr object, int value)",
        NULL,
};

// Emit signal
signal_handler_signal(handler, "event_name", &calldata);

// Connect to signal
signal_handler_connect(handler, "event_name", callback, data);
```

### Threading Model
- Main thread: UI operations
- Video thread: rendering, encoding
- Audio thread: audio mixing
- Use `pthread_mutex_t` for synchronization
- Use `os_event_t` and `os_sem_t` for signaling

---

## 5. CMake Conventions

- **2-space indentation** (not tabs)
- Modern CMake targets
- Prefix with `cmake` in commit messages

---

## 6. Commit Message Format

```
module-name: Short description (max 50 chars)

Detailed explanation wrapped at 72 columns.
Explain the "why" not just the "what".
```

Examples:
- `libobs: Fix source not displaying`
- `obs-ffmpeg: Fix bug with audio output`
- `frontend: Add multi-output stage support`

---

## 7. Language-Specific Notes

### C (libobs core)
- C11 standard features allowed
- Use `DARRAY()` macro for dynamic arrays
- Use `bfree()/bzalloc()/brealloc()` for memory

### C++ (frontend)
- C++17 standard
- Qt 6 framework for UI
- Use Qt smart pointers where applicable

### Objective-C (macOS)
- 4-space indentation
- Clang format with WebKit-style braces

---

## 8. Key Data Structures

### obs_video_info
```c
struct obs_video_info {
        uint32_t fps_num;           // FPS numerator
        uint32_t fps_den;           // FPS denominator
        uint32_t base_width;        // Base canvas width
        uint32_t base_height;       // Base canvas height
        uint32_t output_width;      // Output/scaled width
        uint32_t output_height;     // Output/scaled height
        enum video_format output_format;
        enum video_colorspace colorspace;
        enum video_range_type range;
        enum obs_scale_type scale_type;
};
```

### obs_canvas (key for multi-output)
- Contains: `view`, `mix`, `ovi`, `flags`, `sources`
- Flags: `MAIN`, `PROGRAM`, `ACTIVATE`, `MIX_AUDIO`, `SCENE_REF`, `EPHEMERAL`
- Each canvas can have its own resolution/framerate

### obs_core_video_mix
- GPU encoder pipeline per video mix
- Render textures, copy surfaces
- Conversion and color processing

---

## 9. Error Handling

```c
// Validation macros
if (!obs_output_valid(output, "function_name"))
        return;

// Logging
blog(LOG_ERROR, "Error message: %s", details);
blog(LOG_WARNING, "Warning message");
blog(LOG_DEBUG, "Debug info");
```

---

## 10. Testing

- Test files in `test/` directory
- Use CMake CTest integration
- Unit tests for core library functions

---

## References

- [Linux Kernel Coding Style](https://github.com/torvalds/linux/blob/master/Documentation/process/coding-style.rst)
- [OBS API Documentation](https://obsproject.com/docs)
- `.clang-format` in project root for automated formatting
- `.editorconfig` for editor configuration
