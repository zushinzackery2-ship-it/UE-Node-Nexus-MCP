# Primary viewport diagnostics

`unreal.NexusViewportDiagnosticsLibrary` is part of the engine-installed
`UeNodeNexusBridge`. Its eight methods retain the Shadetest Python names and
run on the editor game thread. Renderer probes use UE 5.5 private structures
and require binaries built for the same engine.

| Python method | Result / effect |
| --- | --- |
| `primary_viewport_state_json()` | Current viewport size, input, exposure, render counters and completed shader phase. `available=false` means the current viewport cannot be sampled. `realtime_override` explains engine realtime overrides. |
| `set_primary_viewport_input_enabled_json(enabled, view_key=0)` | Change camera input and return its previous state/view identity. Disabling clears velocity and camera transition; the transition settles on the following editor tick. |
| `reset_primary_viewport_history_json()` | Reset exposure/occlusion seeds and release previous TSR, TAA and SSR histories through a render fence. |
| `capture_primary_at_phase_json(filename, temporal_sample, render_frame_mod8, timeout_seconds)` | Queue a PNG when the current viewport's completed render counters match. |
| `capture_primary_at_shader_phase_json(filename, temporal_sample, shader_frame_mod8, timeout_seconds)` | Match shader sample, shader frame modulo 8 and output frame modulo 8. |
| `queue_primary_pixel_probe_json(x, y, width, height, temporal_sample, shader_frame_mod8, timeout_seconds=30.0)` | Admit one GPU probe, pinned to the view identity. Dimensions are 1–32 pixels; timeout is 1–120 seconds. Original six-argument calls retain a 30-second deadline. |
| `read_primary_pixel_probe_json()` | Poll JSON state and tightly packed raw texture bytes as Base64. Expiration returns `state=failed`, `error_code=pixel_probe_timeout`. |
| `profile_primary_gpu()` | Request UE GPU profiling. `r.ProfileGPU.ShowUI` controls UE's profile UI. |

Parse JSON and check `applied`, `reset` or `requested` before proceeding. Restore
input with the returned view identity even if viewport selection has changed:

```python
import json
import unreal

library = unreal.NexusViewportDiagnosticsLibrary
disabled = json.loads(library.set_primary_viewport_input_enabled_json(False))
assert disabled["applied"], disabled
try:
    state = json.loads(library.primary_viewport_state_json())
    assert state["available"], state
finally:
    restored = json.loads(library.set_primary_viewport_input_enabled_json(
        disabled["enabled_before"], disabled["view_key"]))
    assert restored["applied"], restored
```

Snapshot/history reads synchronize with the render thread. Sample at the
cadence required by the diagnostic task.

Capture filenames are basenames of at most 100 alphanumeric, underscore or
hyphen characters. Frame modulo is 0–7 and timeout is 1–120 seconds. Poll the
admitted `receipt_path` until `completed` or `failed`. Success includes verified
PNG dimensions, byte count, SHA-1, `requested_phase` and exact `render_state`.
Only one screenshot is admitted. The request uses the target's normal draw.
Target changes fail with `capture_viewport_changed`; an unreachable phase
fails with `capture_timeout` and releases admission.

Probe coordinates are native scene-buffer pixels. Returned textures report
their own rectangle, format and `bytes_per_pixel`; TSR history coordinates
can differ because of resolution scaling. Optional engine textures report
`*_available=false` and the native absence/bounds reason. Velocity and temporal
history depend on the actual renderer output, realtime and AA settings.
Depth is sampled into a small R32F target because D3D12 depth-stencil textures
cannot be directly mapped for CPU readback. Submitted copies remain alive
through expiration until their GPU fences signal, then release before another
probe is admitted.

`LogNexusViewportDiagnostics` records admission, completion, timeout, view
identity and transfer size. Phase captures also retain on-disk receipts.

For ordinary screenshots, `viewport_capture` selects `level`, `active` or `pie`.
Use `pie` to verify the game world while an asset editor has focus. The durable
`producer_view` is frozen during the target's normal screenshot draw: its `world`,
`world_type`, `viewport_type`, `view_key` and `engine_frame` identify the actual view.
The producing frame must match the receipt's frame; unavailable or mismatched view
evidence fails with `capture_producer_mismatch` before requesting the screenshot.
