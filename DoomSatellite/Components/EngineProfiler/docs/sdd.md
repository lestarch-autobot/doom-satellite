# Components::EngineProfiler

## 1. Introduction

`Components::EngineProfiler` reports the per-phase CPU budget of one DOOM tick. The measurements are taken by
link-time wrappers around three engine entry points (`EngineHooks.h`, implemented in `DoomFlight/EngineHooks.c` and
enabled with `-Wl,--wrap=doomgeneric_Tick -Wl,--wrap=I_FinishUpdate -Wl,--wrap=DG_DrawFrame`), so neither the
doomgeneric engine nor `Doom::DoomEngine` is modified. Once per `schedIn` call the component snapshots and resets the
counters and emits, per phase, the longest and the mean duration over the window in microseconds.

## 2. Phases

All phases run on the DOOM rate group thread and partition one `doomgeneric_Tick`:

| Phase | From | To | Content |
| --- | --- | --- | --- |
| `Tick` | `doomgeneric_Tick` entry | return | whole tick |
| `Render` | tick entry | first `I_FinishUpdate` of the tick | game logic and software rendering into `I_VideoBuffer` |
| `Blit` | `I_FinishUpdate` entry | `DG_DrawFrame` entry | the `I_VideoBuffer` to `DG_ScreenBuffer` copy |
| `Emit` | `DG_DrawFrame` entry | return | `Doom::DoomEngine::platformDrawFrame` and everything wired downstream of `frameOut` |

A wipe (level transition) draws several frames in one tick; each draw records a `Blit` and an `Emit` sample, and the
`Render` phase ends at the first draw. Melt frames replayed by `Doom::DoomEngine::schedIn_handler` do not pass through
`DG_DrawFrame` and are not sampled.

## 3. Telemetry

`Ticks`, and `<Phase>MaxUs` / `<Phase>AvgUs` for `Tick`, `Render`, `Blit`, `Emit`. The clock is the Zephyr cycle
counter (`k_cycle_get_32`, CPU clock on the Teensy 4.1).
