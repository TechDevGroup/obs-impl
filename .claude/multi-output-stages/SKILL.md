# Multi-Output Stages Feature Implementation

> **DIRECTIVE**: Consult this skill document FIRST before reading source files. This document serves as the authoritative reference for the multi-output stages feature. Only read source files when specific implementation details are not documented here. Update this document as you discover implementation details.

---

## 1. Feature Overview

### Goal
Enable a single OBS instance to:
- Manage multiple "stages" (canvases) with different aspect ratios (e.g., 16:9 and 9:16)
- Route each stage to separate, independent output targets
- Reuse sources/resources across stages efficiently
- Provide UI for configuring stage-to-output mappings

### Use Cases
- Simultaneous streaming to landscape (YouTube/Twitch) and portrait (TikTok/Reels) platforms
- Multiple quality tiers to different CDN endpoints
- Recording + streaming with different layouts
- Multi-camera production with isolated outputs

---

## 2. Current Architecture Analysis

### 2.1 Canvas System (`libobs/obs-canvas.c`)
**Status: FULLY DOCUMENTED**

The canvas system is the foundation for multi-stage support:

```
obs_canvas_t
├── context (name, uuid, signals)
├── view (obs_view_t - holds source channels)
├── mix (obs_core_video_mix* - video rendering pipeline)
├── ovi (obs_video_info - resolution/fps config)
├── flags (MAIN, PROGRAM, ACTIVATE, MIX_AUDIO, etc.)
└── sources (linked list of canvas-owned sources)
```

**Key APIs:**
- `obs_canvas_create(name, ovi, flags)` - create new canvas
- `obs_canvas_create_private(name, ovi, flags)` - create non-public canvas
- `obs_canvas_set_channel(canvas, channel, source)` - assign source to render channel
- `obs_canvas_get_video(canvas)` - get video_t for canvas
- `obs_canvas_reset_video(canvas, ovi)` - change resolution/fps
- `obs_get_canvas_by_uuid(uuid)` - lookup canvas by UUID
- `obs_get_main_canvas()` - get the main canvas

**Findings:**
- Each canvas can have its own `obs_video_info` (different resolution/fps)
- Canvas has its own `view` for channel management
- Canvas creates/owns its `video_mix` for GPU rendering
- Main canvas is special (MAIN flag, cannot be removed/renamed)
- Canvases are stored in hash tables by UUID and name

### 2.2 View System (`libobs/obs-view.c`)
**Status: FULLY DOCUMENTED**

Views manage source channel assignments:

```c
struct obs_view {
        pthread_mutex_t channels_mutex;
        obs_source_t *channels[MAX_CHANNELS];
        enum view_type type;  // MAIN_VIEW or AUX_VIEW
};
```

**Key APIs:**
- `obs_view_set_source(view, channel, source)` - assign source
- `obs_view_render(view)` - render all channels
- `obs_view_add2(view, ovi)` - create video mix for view

### 2.3 Video Mix (`obs-internal.h`)
**Status: FULLY DOCUMENTED**

```c
struct obs_core_video_mix {
        struct obs_view *view;           // Associated view
        video_t *video;                  // Video output handle
        struct obs_video_info ovi;       // Resolution/format config
        gs_texture_t *render_texture;    // GPU render target
        gs_texture_t *output_texture;    // Final output texture
        DARRAY(obs_encoder_t *) gpu_encoders;  // Attached encoders
        bool mix_audio;                  // Include in audio mix
        // ... GPU encoding infrastructure
};
```

### 2.4 Output System (`libobs/obs-output.c`)
**Status: FULLY DOCUMENTED**

```c
struct obs_output {
        struct obs_context_data context;
        struct obs_output_info info;

        video_t *video;                                    // Direct video source (raw outputs)
        audio_t *audio;                                    // Direct audio source (raw outputs)
        obs_encoder_t *video_encoders[MAX_OUTPUT_VIDEO_ENCODERS];  // Up to 10 video encoders
        obs_encoder_t *audio_encoders[MAX_OUTPUT_AUDIO_ENCODERS];  // Up to 6 audio encoders
        obs_service_t *service;                            // Streaming service config

        volatile bool active;
        volatile bool paused;
        // ... reconnection, delay, interleaving state
};
```

**Key APIs:**
- `obs_output_set_video_encoder(output, encoder)` - assign primary video encoder
- `obs_output_set_video_encoder2(output, encoder, idx)` - assign video encoder at index
- `obs_output_set_audio_encoder(output, encoder, idx)` - assign audio encoder
- `obs_output_set_service(output, service)` - assign streaming service
- `obs_output_start(output)` / `obs_output_stop(output)` - control output
- `obs_output_begin_data_capture(output, flags)` - start receiving encoder data

**Output Flow:**
1. Output created with `obs_output_create(id, name, settings, hotkey_data)`
2. Encoders assigned via `obs_output_set_video_encoder2()`
3. Service assigned via `obs_output_set_service()` (for streaming)
4. `obs_output_start()` calls output plugin's `start()` function
5. `obs_output_begin_data_capture()` hooks encoder callbacks
6. Encoded packets flow through `hook_data_capture()` to output plugin

**Multi-Track Video Support:**
- `OBS_OUTPUT_MULTI_TRACK_VIDEO` flag enables multiple video encoder slots
- Up to `MAX_OUTPUT_VIDEO_ENCODERS` (10) video encoders per output
- Each encoder can come from a different canvas/video source

### 2.5 Encoder System (`libobs/obs-encoder.c`)
**Status: FULLY DOCUMENTED**

```c
struct obs_encoder {
        struct obs_context_data context;
        struct obs_encoder_info info;

        void *media;                    // video_t* or audio_t* depending on type
        uint32_t timebase_num;
        uint32_t timebase_den;
        video_t *fps_override;          // Optional frame rate override

        uint32_t scaled_width;
        uint32_t scaled_height;
        uint32_t frame_rate_divisor;
        // ... encoding state
};
```

**Key APIs:**
- `obs_encoder_set_video(encoder, video)` - set video source (from canvas)
- `obs_encoder_set_audio(encoder, audio)` - set audio source
- `obs_encoder_set_scaled_size(encoder, width, height)` - set output resolution
- `obs_encoder_set_group(encoder, group)` - add to encoder group
- `obs_video_encoder_create(id, name, settings, hotkey_data)` - create encoder

**Encoder-Canvas Relationship:**
```c
// Connect encoder to canvas video
video_t *video = obs_canvas_get_video(canvas);
obs_encoder_set_video(encoder, video);
```

**Critical Finding:** The encoder's video source (`encoder->media`) is set to a `video_t*` obtained from a canvas. This is the key linkage that allows different encoders to encode from different canvases.

### 2.6 Existing Multi-Canvas Support (`frontend/utility/MultitrackVideoOutput.cpp`)
**Status: FULLY DOCUMENTED - CRITICAL FINDING**

OBS already has multi-canvas support in the multitrack video output system:

```cpp
struct OBSOutputObjects {
        OBSOutputAutoRelease output_;
        std::shared_ptr<obs_encoder_group_t> video_encoder_group_;
        std::vector<OBSEncoderAutoRelease> audio_encoders_;
        OBSServiceAutoRelease multitrack_video_service_;
        std::vector<OBSCanvasAutoRelease> canvases;  // MULTIPLE CANVASES!
};
```

**How It Works:**
1. `PrepareStreaming()` accepts optional `extra_canvas` UUID parameter
2. Creates vector of canvases: main canvas + extra canvases
3. Each encoder config specifies `canvas_index` to select which canvas
4. Encoders are created and linked to specific canvases:
   ```cpp
   auto &canvas = canvases[config.canvas_index];
   obs_encoder_set_video(video_encoder, obs_canvas_get_video(canvas));
   ```

**This is the exact pattern we need!** The multitrack video system proves that:
- Multiple canvases with different resolutions work
- Encoders can be linked to specific canvases
- Multiple encoded streams can go to a single output

**Our Extension:** Instead of multiple canvases -> one output, we need:
- Multiple canvases (stages)
- Each stage -> one or more independent outputs

### 2.7 Frontend UI Patterns
**Status: DOCUMENTED**

**BasicOutputHandler** (`frontend/utility/BasicOutputHandler.hpp`):
- Manages streaming, recording, replay buffer, virtual cam outputs
- Has `multitrackVideo` member for multi-canvas streaming
- Virtual cam already uses separate view/video: `obs_view_t *virtualCamView`

**Multiview** (`frontend/components/Multiview.cpp`):
- Renders multiple scene previews in configurable grid layouts
- Uses `startRegion()`/`endRegion()` for viewport management
- Helpful pattern for stage preview rendering

---

## 3. Implementation Plan (Revised)

### Key Insight: Leverage Existing Infrastructure

Given that OBS already supports:
- Multiple canvases with `obs_canvas_create()`
- Encoder-to-canvas binding with `obs_encoder_set_video()`
- Multi-track video output with multiple encoders

We should **extend** the existing systems rather than create parallel infrastructure.

### Phase 1: Stage Manager (Lightweight Wrapper)

#### 1.1 Stage Structure
**Location:** `libobs/obs-stage.c` (new file)

```c
struct obs_stage {
        obs_canvas_t *canvas;              // Underlying canvas
        char *name;                        // Display name
        DARRAY(obs_output_t *) outputs;    // Independent outputs for this stage
        uint32_t flags;
        signal_handler_t *signals;

        // Convenience
        bool streaming_active;
        bool recording_active;
};

// API
obs_stage_t *obs_stage_create(const char *name, struct obs_video_info *ovi);
void obs_stage_destroy(obs_stage_t *stage);
obs_canvas_t *obs_stage_get_canvas(obs_stage_t *stage);

// Output management
void obs_stage_add_output(obs_stage_t *stage, obs_output_t *output);
void obs_stage_remove_output(obs_stage_t *stage, obs_output_t *output);
bool obs_stage_start_output(obs_stage_t *stage, size_t output_idx);
void obs_stage_stop_output(obs_stage_t *stage, size_t output_idx);
void obs_stage_start_all(obs_stage_t *stage);
void obs_stage_stop_all(obs_stage_t *stage);

// Scene management
void obs_stage_set_scene(obs_stage_t *stage, obs_scene_t *scene);
obs_scene_t *obs_stage_get_scene(obs_stage_t *stage);
```

**Tasks:**
- [x] Design leverages existing canvas system
- [ ] Implement stage structure wrapping canvas
- [ ] Implement output array management
- [ ] Add signal handlers for stage events
- [ ] Integrate with global stage registry

#### 1.2 Encoder Factory Per Stage
**Pattern from MultitrackVideoOutput:**

```c
// For each output on a stage:
obs_encoder_t *encoder = obs_video_encoder_create(encoder_id, name, settings, NULL);
obs_encoder_set_video(encoder, obs_canvas_get_video(stage->canvas));
obs_encoder_set_scaled_size(encoder, width, height);
obs_output_set_video_encoder(output, encoder);
```

**Tasks:**
- [ ] Create helper to spawn encoder for stage
- [ ] Handle encoder cleanup on stage/output removal
- [ ] Support encoder sharing between outputs on same stage

### Phase 2: Frontend UI

#### 2.1 Stage Dashboard
**Location:** `frontend/components/StageManager.cpp` (new)

Based on Multiview patterns:
```cpp
class StageManager {
public:
        void Update();
        void Render(uint32_t cx, uint32_t cy);

private:
        std::vector<OBSStage> stages;
        // Per-stage preview rendering using startRegion/endRegion
};
```

**Tasks:**
- [ ] Create StageManager widget
- [ ] Implement stage card with preview
- [ ] Add stage creation dialog
- [ ] Implement drag-drop output assignment

#### 2.2 Output Configuration Per Stage
**Tasks:**
- [ ] Design per-stage output list UI
- [ ] Encoder settings per output
- [ ] Quick toggle for start/stop individual outputs

### Phase 3: Integration

#### 3.1 Scene Collection Support
**Tasks:**
- [ ] Extend save format for stages
- [ ] Load stages on collection load
- [ ] Handle missing canvases gracefully

#### 3.2 Hotkey Support
**Tasks:**
- [ ] Per-stage scene switching hotkeys
- [ ] Per-output start/stop hotkeys

---

## 4. Technical Considerations

### 4.1 Performance
- **GPU Texture Sharing:** Sources rendered once, shared across canvases
- **Encoder Threads:** Each encoder runs in its own thread
- **Memory:** Each canvas has its own render textures (~30-50MB per 1080p canvas)

### 4.2 Audio Routing
- Each canvas can optionally mix audio (`MIX_AUDIO` flag)
- Consider per-stage audio mixer selection
- VOD track support per stage

### 4.3 Compatibility
- Existing scene collections work (single main canvas = single stage)
- Plugin outputs work unchanged
- Multitrack video can coexist (use for single-output multi-encoder scenarios)

---

## 5. File Inventory

### New Files
- `libobs/obs-stage.c` - Stage management
- `libobs/obs-stage.h` - Stage public API
- `frontend/components/StageManager.cpp/.hpp` - Stage dashboard UI

### Modified Files
- `libobs/obs.h` - Export stage API
- `libobs/obs-internal.h` - Stage in core data
- `libobs/CMakeLists.txt` - Build obs-stage.c
- `frontend/CMakeLists.txt` - Build StageManager
- `frontend/widgets/OBSBasic.cpp` - Initialize stage manager

---

## 6. Investigation Checklist

- [x] `obs-output.c`: Output-encoder-video binding flow
- [x] `obs-encoder.c`: Encoder video source assignment
- [x] `frontend/utility/MultitrackVideoOutput.cpp`: Multi-canvas pattern
- [x] `frontend/utility/BasicOutputHandler.hpp`: Output handler structure
- [x] `frontend/components/Multiview.cpp`: Multi-preview rendering
- [ ] `plugins/obs-outputs/`: RTMP/recording implementations (if needed)
- [ ] Scene collection save/load format (when implementing persistence)

---

## 7. Progress Tracking

| Phase | Component | Status | Notes |
|-------|-----------|--------|-------|
| Research | Architecture Analysis | **Complete** | Multi-canvas already supported |
| 1.1 | Stage Structure | Ready to Start | Design finalized |
| 1.2 | Encoder Factory | Ready to Start | Pattern from MultitrackVideoOutput |
| 2.1 | Stage Dashboard UI | Not Started | |
| 2.2 | Output Config UI | Not Started | |
| 3.1 | Scene Collection | Not Started | |
| 3.2 | Hotkeys | Not Started | |

---

## Appendix: Quick Reference

### Canvas Flags
```c
enum canvas_flags {
        MAIN       = (1 << 0),  // Primary canvas (cannot remove)
        PROGRAM    = (1 << 1),  // Program output
        ACTIVATE   = (1 << 2),  // Auto-activate sources
        MIX_AUDIO  = (1 << 3),  // Include in audio mix
        SCENE_REF  = (1 << 4),  // Hold scene references
        EPHEMERAL  = (1 << 5),  // Don't persist
};
```

### Output Flags
```c
#define OBS_OUTPUT_VIDEO             (1 << 0)
#define OBS_OUTPUT_AUDIO             (1 << 1)
#define OBS_OUTPUT_ENCODED           (1 << 2)
#define OBS_OUTPUT_SERVICE           (1 << 3)
#define OBS_OUTPUT_MULTI_TRACK       (1 << 4)  // Multi-track audio
#define OBS_OUTPUT_MULTI_TRACK_VIDEO (1 << 6)  // Multi-track video
```

### Key Constants
```c
#define MAX_CHANNELS              64
#define MAX_OUTPUT_VIDEO_ENCODERS 10
#define MAX_OUTPUT_AUDIO_ENCODERS 6
```

### Critical Code Patterns

**Create encoder for specific canvas:**
```c
obs_encoder_t *encoder = obs_video_encoder_create("obs_x264", "my_encoder", settings, NULL);
obs_encoder_set_video(encoder, obs_canvas_get_video(canvas));
obs_encoder_set_scaled_size(encoder, 1920, 1080);
```

**Assign encoder to output:**
```c
obs_output_set_video_encoder(output, encoder);
obs_output_set_service(output, service);
obs_output_start(output);
```

**Get canvas by UUID:**
```c
obs_canvas_t *canvas = obs_get_canvas_by_uuid(uuid_string);
```
